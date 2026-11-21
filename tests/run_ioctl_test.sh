#!/usr/bin/env bash
# Load dvfel on the Canadian test box, run the interface tests, unload, and report.
# Refuses any device other than the test box.
set -uo pipefail
T=${ONN_BOX:?set ONN_BOX to the box adb address, e.g. ONN_BOX=192.168.1.20:5555}
P=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
R=/data/local/tmp
su_do() { timeout "${2:-30}" adb -s "$T" shell "su -c '$1'" 2>&1; }

[ "$(adb -s "$T" shell getprop ro.build.version.incremental | tr -d '\r')" = 15016191 ] ||
  { echo "not the Canadian test firmware, stopping"; exit 3; }

echo "=== build"
"$P/kernel/clang-r487747c/bin/clang" --target=armv7a-linux-gnueabi -march=armv7-a -marm -mfloat-abi=soft \
  -O2 -Wall -Wextra -Wno-unused-parameter -ffreestanding -fno-stack-protector -fno-builtin -nostdlib -static \
  -fuse-ld=lld -I"$P/src/fel/onn" -I"$P/kernel/out/usr/include" \
  -o "$P/tests/dvfel_ioctl_test" "$P/tests/dvfel_ioctl_test.c" || exit 1
file "$P/tests/dvfel_ioctl_test" | cut -d: -f2 | cut -c1-90

echo "=== load"
adb -s "$T" push "$P/dist/dvfel.ko" "$R/dvfel.ko" | tail -1 | cut -c1-90
adb -s "$T" push "$P/tests/dvfel_ioctl_test" "$R/dvfel_ioctl_test" | tail -1 | cut -c1-90
adb -s "$T" shell chmod 755 "$R/dvfel_ioctl_test"
up0=$(adb -s "$T" shell cut -d. -f1 /proc/uptime | tr -d '\r')
su_do "dmesg -c >/dev/null; mount -t debugfs none /sys/kernel/debug; insmod $R/dvfel.ko open_driver_fixed=1 debug=1; echo insmod_rc=\$?" 40

echo "=== interface checks"
su_do "$R/dvfel_ioctl_test; echo test_rc=\$?" 60

echo "=== live session"
timeout 20 adb -s "$T" shell "su -c '$R/dvfel_ioctl_test hold 6'" &
hold_pid=$!
python3 -I -c 'import time; time.sleep(2.5)'
su_do "cat /sys/kernel/debug/dvfel/stats | grep -E \"^(fixes session|version|el in|hints in)\"; echo --- vfm; cat /sys/class/vfm/map 2>/dev/null | grep -iE \"dvel|dvbl\"; echo --- providers; cat /sys/class/vfm/provider 2>/dev/null | grep -i dvel"
wait $hold_pid
python3 -I -c 'import time; time.sleep(1.5)'

echo "=== after session"
su_do "cat /sys/kernel/debug/dvfel/stats | grep -E \"^(fixes session|draining|version)\"; grep -E \"^dvfel \" /proc/modules | cut -d\" \" -f1-3"

echo "=== unload"
su_do "rmmod dvfel; echo rmmod_rc=\$?" 40
su_do "echo loaded=\$(grep -c \"^dvfel \" /proc/modules); ls /dev/dvfel 2>&1"

echo "=== kernel log"
su_do "dmesg | grep -E \"dvfel\" | cut -c1-150 | tail -32"
echo "--- faults"
su_do "dmesg | grep -ciE \"Oops|BUG:|Unable to handle|CFI failure|kernel panic|WARNING: CPU|Internal error\""

echo "=== cleanup"
su_do "umount /sys/kernel/debug; rm -f $R/dvfel.ko $R/dvfel_ioctl_test; ls $R/dvfel.ko $R/dvfel_ioctl_test 2>&1 | wc -l"
up1=$(adb -s "$T" shell cut -d. -f1 /proc/uptime | tr -d '\r')
echo "uptime $up0 -> $up1 (a lower second value means the box rebooted)"
adb -s "$T" shell 'getprop sys.boot_completed; tail -1 /sys/class/amdolby_vision/dv_mode 2>/dev/null' | tr -d '\r'
