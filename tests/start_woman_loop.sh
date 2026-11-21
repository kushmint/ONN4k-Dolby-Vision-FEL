#!/usr/bin/env bash
# Leave the "woman at 80s" FEL test looping on the test box for a visual check.
# Usage: start_woman_loop.sh [module args] (default composer=1)
set -uo pipefail
T=${ONN_BOX:?set ONN_BOX to the box adb address, e.g. ONN_BOX=192.168.1.20:5555}
P=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
R=/data/local/tmp
PKG=org.dvfel.dvfptest
ARGS=${1:-composer=1}
LOOPS=${LOOPS:-30}
a() { timeout "${TMO:-30}" adb -s "$T" shell "$@" 2>&1; }
su_do() { timeout "${2:-30}" adb -s "$T" shell "su -c '$1'" 2>&1; }
pause() { python3 -I -c "import time; time.sleep($1)"; }

[ "$(a getprop ro.build.version.incremental | tr -d '\r')" = 15016191 ] ||
  { echo "not the Canadian test firmware, stopping"; exit 3; }

if [ "$(a grep -c '^dvfel ' /proc/modules | tr -d '\r')" != 0 ]; then
  echo "=== rebooting to clear the previous module"
  a am force-stop $PKG
  adb -s "$T" reboot
  pause 25
  for _ in $(seq 1 30); do
    adb connect "$T" >/dev/null 2>&1
    [ "$(a getprop sys.boot_completed | tr -d '\r')" = 1 ] && break
    pause 5
  done
  pause 15
fi

adb -s "$T" push "$P/dist/dvfel.ko" "$R/dvfel.ko" | tail -1 | cut -c1-80
adb -s "$T" push "$P/tests/play_setup_device.sh" "$R/play_setup.sh" | tail -1 | cut -c1-80
su_do "dmesg -c >/dev/null; DVFEL_ARGS=\"$ARGS\" sh $R/play_setup.sh; echo setup_rc=\$?" 40 | tail -1
a input keyevent KEYCODE_WAKEUP
pause 5
a am force-stop $PKG
a setprop debug.dvfp.test.fel 1
a setprop debug.dvfp.test.loops "$LOOPS"
a setprop debug.dvfp.test.codec c2.amlogic.dolby-vision.dvhe.decoder
a am start -n $PKG/android.app.NativeActivity | tail -1 | cut -c1-100

# Stay past the first loop point so a failed restart is caught now.
for t in 30 140; do
  [ "$t" = 30 ] && pause 30 || pause 110
  echo "--- t=${t}s app_pid=$(a pidof $PKG | tr -d '\r')"
  su_do "grep -E \"^(fixes session|el in|bl paired)\" /sys/kernel/debug/dvfel/stats; echo hdr=\$(cat /sys/class/amhdmitx/amhdmitx0/hdmi_hdr_status) mode=\$(cat /sys/class/display/mode)" | tr -d '\r' | cut -c1-110
done
a dumpsys power | grep -E 'mWakefulness=' | tr -d '\r'
echo "faults: $(su_do dmesg 30 | grep -ciE 'Oops|BUG:|Unable to handle|CFI failure|kernel panic|WARNING: CPU|Internal error')"
