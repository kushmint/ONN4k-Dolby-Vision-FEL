#!/usr/bin/env bash
# Play the "woman at 80s" FEL layer test on the Canadian box and grab the real
# video output through VDIN1. The woman exists only in the enhancement layer.
# Usage: run_woman_capture.sh <label> <au file> [module args]
set -uo pipefail
T=${ONN_BOX:?set ONN_BOX to the box adb address, e.g. ONN_BOX=192.168.1.20:5555}
P=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
R=/data/local/tmp
PKG=org.dvfel.dvfptest
LABEL=$1 AU=$2 ARGS=${3:-}
FIRST=${FIRST:-60} LAST=${LAST:-112}
LOG="$P/tests/logs/woman-$LABEL-$(date +%H%M%S)"
mkdir -p "$LOG"
a() { timeout "${TMO:-30}" adb -s "$T" shell "$@" 2>&1; }
su_do() { timeout "${2:-30}" adb -s "$T" shell "su -c '$1'" 2>&1; }
pause() { python3 -I -c "import time; time.sleep($1)"; }

[ "$(a getprop ro.build.version.incremental | tr -d '\r')" = 15016191 ] ||
  { echo "not the Canadian test firmware, stopping"; exit 3; }

if [ "$(a grep -c '^dvfel ' /proc/modules | tr -d '\r')" != 0 ]; then
  echo "=== rebooting to clear the previous module"
  adb -s "$T" reboot
  pause 25
  for _ in $(seq 1 30); do
    adb connect "$T" >/dev/null 2>&1
    [ "$(a getprop sys.boot_completed | tr -d '\r')" = 1 ] && break
    pause 5
  done
  pause 15
fi

echo "=== install"
adb -s "$T" push "$P/dist/dvfel.ko" "$R/dvfel.ko" | tail -1 | cut -c1-80
adb -s "$T" push "$P/tests/play_setup_device.sh" "$R/play_setup.sh" | tail -1 | cut -c1-80
timeout 90 adb -s "$T" install -r -g "$P/player/build/dvfptest.apk" 2>&1 | tail -1
a mkdir -p /sdcard/Android/data/$PKG/files
if [ "$(a stat -c %s /sdcard/Android/data/$PKG/files/test.au | tr -d '\r')" != "$(stat -c %s "$AU")" ]; then
  timeout 200 adb -s "$T" push "$AU" /sdcard/Android/data/$PKG/files/test.au | tail -1 | cut -c1-90
fi

echo "=== load ($ARGS)"
if [ -z "${NOMODULE:-}" ]; then
  su_do "dmesg -c >/dev/null; DVFEL_ARGS=\"$ARGS\" sh $R/play_setup.sh; echo setup_rc=\$?" 40 | tail -2
else
  echo "module not loaded for this run"
fi
a input keyevent KEYCODE_WAKEUP
pause 8
if [ -n "${LLDV:-}" ]; then
  adb -s "$T" push "$P/tests/lldv_setup_device.sh" "$R/lldv_setup.sh" >/dev/null
  su_do "sh $R/lldv_setup.sh $LLDV" 60 | tr -d '\r' | tee "$LOG/lldv.txt"
  grep -q '^LLDV_READY' "$LOG/lldv.txt" || { echo "LLDV output is not active, stopping"; exit 4; }
  [ -n "${LLDV_META:-}" ] && su_do "cd /sys/module/dvfel/parameters; $LLDV_META"
fi
su_do "setprop ctl.start screen_control; rm -rf /data/temp; mkdir -p /data/temp; chmod 777 /data/temp"
a input keyevent KEYCODE_WAKEUP
pause 5

echo "=== play"
a am force-stop $PKG
a logcat -c
a setprop debug.dvfp.test.fel "${FEL:-1}"
a setprop debug.dvfp.test.loops 1
a setprop debug.dvfp.test.codec "${CODEC:-c2.amlogic.dolby-vision.dvhe.decoder}"
a am start -n $PKG/android.app.NativeActivity | tail -1 | cut -c1-100
START=$(date +%s)
pause "$FIRST"

echo "=== capture"
su_do "echo disable_video=\$(cat /sys/class/video/disable_video); cat /sys/class/video/video_state; cat /sys/class/video/frame_width /sys/class/video/frame_height; cat /sys/class/vfm/map" > "$LOG/video-state.txt"
adb -s "$T" push "$P/tests/state_dump_device.sh" "$R/state_dump.sh" >/dev/null
[ -n "${STATE:-}" ] && su_do "sh $R/state_dump.sh" 60 | tr -d '\r' > "$LOG/state.txt"
adb -s "$T" push "$P/tests/state_dump_device.sh" "$R/state_dump.sh" >/dev/null
[ -n "${STATE:-}" ] && su_do "sh $R/state_dump.sh" 60 | tr -d '\r' > "$LOG/state.txt"
n=0
while [ $(($(date +%s) - START)) -lt "$LAST" ]; do
  for type in ${TYPES:-1}; do
    n=$((n + 1))
    at=$(($(date +%s) - START))
    su_do "cd /data/temp; /vendor/bin/screencatch -m -t $type -p /data/temp 0 0 3840 2160 960 540 >/dev/null 2>&1; mv /data/temp/1.bmp /sdcard/cap_$n.bmp" 20 >/dev/null
    adb -s "$T" pull "/sdcard/cap_$n.bmp" "$LOG/cap_$(printf %03d $n)_type${type}_t$at.bmp" >/dev/null 2>&1
    a rm -f "/sdcard/cap_$n.bmp"
  done
done
echo "captures: $n"

su_do "grep -E \"^(fixes session|el in|bl paired)\" /sys/kernel/debug/dvfel/stats; cat /sys/kernel/debug/dvfel/regs" > "$LOG/stats.txt"
su_do "echo hdr=\$(cat /sys/class/amhdmitx/amhdmitx0/hdmi_hdr_status) attr=\$(cat /sys/class/amhdmitx/amhdmitx0/attr) mode=\$(cat /sys/class/display/mode) ll=\$(cat /sys/module/aml_media/parameters/dolby_vision_ll_policy); tail -3 /sys/class/amdolby_vision/dv_mode" | tr -d '\r' | tee -a "$LOG/stats.txt"
su_do "grep ^lldv /sys/kernel/debug/dvfel/regs; cat /sys/class/amhdmitx/amhdmitx0/hdmitx_pkt_dump" 20 | tr -d '\r' > "$LOG/hdmi-packets.txt"
a am force-stop $PKG
pause 3
TMO=25 a logcat -d > "$LOG/logcat.txt"
su_do dmesg 30 > "$LOG/dmesg.txt"
echo "faults: $(grep -ciE 'Oops|BUG:|Unable to handle|CFI failure|kernel panic|WARNING: CPU|Internal error' "$LOG/dmesg.txt")"
grep -E '^(fixes session|el in|bl paired)' "$LOG/stats.txt" | cut -c1-120
echo "logs: $LOG"
