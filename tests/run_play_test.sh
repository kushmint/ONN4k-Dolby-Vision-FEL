#!/usr/bin/env bash
# Play the FEL test clip on the Canadian test box and record driver behaviour.
# Refuses any device other than the test box.
set -uo pipefail
T=${ONN_BOX:?set ONN_BOX to the box adb address, e.g. ONN_BOX=192.168.1.20:5555}
P=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
R=/data/local/tmp
PKG=org.dvfel.dvfptest
LOG="$P/tests/logs/play-$(date +%H%M%S)"
mkdir -p "$LOG"
a() { timeout "${TMO:-30}" adb -s "$T" shell "$@" 2>&1; }
su_do() { timeout "${2:-30}" adb -s "$T" shell "su -c '$1'" 2>&1; }

[ "$(a getprop ro.build.version.incremental | tr -d '\r')" = 15016191 ] ||
  { echo "not the Canadian test firmware, stopping"; exit 3; }

if [ "$(a grep -c '^dvfel ' /proc/modules | tr -d '\r')" != 0 ]; then
  echo "=== rebooting to clear the previous module"
  adb -s "$T" reboot
  python3 -I -c 'import time; time.sleep(25)'
  for i in $(seq 1 30); do
    adb connect "$T" >/dev/null 2>&1
    [ "$(a getprop sys.boot_completed | tr -d '\r')" = 1 ] && break
    python3 -I -c 'import time; time.sleep(5)'
  done
  python3 -I -c 'import time; time.sleep(15)'
fi

echo "=== install"
adb -s "$T" push "$P/dist/dvfel.ko" "$R/dvfel.ko" | tail -1 | cut -c1-80
adb -s "$T" push "$P/tests/play_setup_device.sh" "$R/play_setup.sh" | tail -1 | cut -c1-80
timeout 90 adb -s "$T" install -r -g "$P/player/build/dvfptest.apk" 2>&1 | tail -1
a mkdir -p /sdcard/Android/data/$PKG/files
if [ "$(a stat -c %s /sdcard/Android/data/$PKG/files/test.au | tr -d '\r')" != "$(stat -c %s "$P/test-media/grinch-fel-60s.au")" ]; then
  timeout 200 adb -s "$T" push "$P/test-media/grinch-fel-60s.au" /sdcard/Android/data/$PKG/files/test.au | tail -1 | cut -c1-90
fi

echo "=== load"
up0=$(a cut -d. -f1 /proc/uptime | tr -d '\r')
su_do "dmesg -c >/dev/null; DVFEL_ARGS=${DVFEL_ARGS:-} sh $R/play_setup.sh; echo setup_rc=\$?" 40

echo "=== wake"
a input keyevent KEYCODE_WAKEUP
for i in 1 2 3 4 5 6 7 8 9 10; do
  a dumpsys power | grep -q 'mWakefulness=Awake' && [ "$(su_do 'cat /sys/class/display/mode' | tr -d '\r')" != dummy_l ] && break
  python3 -I -c 'import time; time.sleep(1.5)'
done
a dumpsys power | grep -E 'mWakefulness=' | tr -d '\r'
su_do 'echo mode=$(cat /sys/class/display/mode) attr=$(cat /sys/class/amhdmitx/amhdmitx0/attr) hpd=$(cat /sys/class/amhdmitx/amhdmitx0/hpd_state)' | tr -d '\r'

echo "=== play"
a am force-stop $PKG
a logcat -c
a setprop debug.dvfp.test.fel 1
a setprop debug.dvfp.test.loops 1
a setprop debug.dvfp.test.codec c2.amlogic.dolby-vision.dvhe.decoder
adb -s "$T" push "$P/tests/crc_poll_device.sh" "$R/crc_poll.sh" >/dev/null
timeout 85 adb -s "$T" shell "su -c 'sh $R/crc_poll.sh 72 ${CERT:-0}'" > "$LOG/crc.raw" 2>&1 &
CRC_PID=$!
a am start -n $PKG/android.app.NativeActivity | tail -1 | cut -c1-100
for t in 8 20 32 44 56 68; do
  python3 -I -c 'import time; time.sleep(11)'
  echo "--- t=${t}s"
  su_do "grep -E \"^(fixes session|el in|bl paired|hints in|wait checks)\" /sys/kernel/debug/dvfel/stats; echo hdr=\$(cat /sys/class/amhdmitx/amhdmitx0/hdmi_hdr_status) \$(tail -1 /sys/class/amdolby_vision/dv_mode); echo src=\$(cat /sys/class/amdolby_vision/src_format 2>/dev/null) fps=\$(cat /sys/class/video/fps_info 2>/dev/null | head -c 70)" | tr -d '\r' | tee -a "$LOG/samples.txt"
  [ "$t" = 32 ] && su_do "cat /sys/kernel/debug/dvfel/regs" > "$LOG/regs.txt"
  a pidof $PKG >/dev/null || { echo "app is no longer running"; break; }
done

echo "=== stop"
a am force-stop $PKG
python3 -I -c 'import time; time.sleep(3)'
TMO=25 a logcat -d > "$LOG/logcat.txt"
su_do "cat /sys/kernel/debug/dvfel/stats" > "$LOG/stats.txt"
su_do "cat /sys/kernel/debug/dvfel/regs" > "$LOG/regs-final.txt"
wait "$CRC_PID" 2>/dev/null
tr -d "\r" < "$LOG/crc.raw" | grep -E "^(0x)?[0-9a-fA-F]{8}$" > "$LOG/crc.txt"
su_do "dmesg" 30 > "$LOG/dmesg.txt"
echo "=== final stats"
grep -E '^(fixes session|draining|el in|bl paired|wait checks|hints in|owned_pair)' "$LOG/stats.txt" | cut -c1-120

echo "=== player log"
grep -E 'dvfp_test|dvfelplay' "$LOG/logcat.txt" | sed -E 's/^[0-9-]+ //' | cut -c1-175 | head -45
echo "--- player errors"
grep -E ' [EF] (dvfp_test|dvfelplay)|FATAL|avc: *denied.*dvfp|avc: *denied.*dvfel' "$LOG/logcat.txt" | cut -c1-200 | head -14

echo "=== module log"
grep -E 'dvfel:' "$LOG/dmesg.txt" | sed -E 's/^\[ *[0-9.]+\] //' | cut -c1-155 | head -40
echo "--- faults: $(grep -ciE 'Oops|BUG:|Unable to handle|CFI failure|kernel panic|WARNING: CPU|Internal error' "$LOG/dmesg.txt")"

echo "=== unload"
su_do "rmmod dvfel; echo rmmod_rc=\$?; echo loaded=\$(grep -c \"^dvfel \" /proc/modules); umount /sys/kernel/debug 2>/dev/null; rm -f $R/dvfel.ko $R/play_setup.sh" 40
up1=$(a cut -d. -f1 /proc/uptime | tr -d '\r')
echo "uptime $up0 -> $up1 (a lower second value means the box rebooted)"
echo "logs: $LOG"
