#!/usr/bin/env bash
# Module regression on the test box (needs the box lock free; each step takes it).
# 1. woman FEL play at 60 Hz with the Magisk module's LLDV session (feldet edge 2.6+ = FEL)
# 2. Grinch FEL at 2160p24 (video_composer player_drop, paired, no_el)
# 3. Grinch FEL stopped with Back, then killed (drain, restore counters, rdma warnings)
# 4. no-FEL play after the kill (green snow check: mean G must not dominate)
# Usage: ONN_BOX=<ip>:5555 tests/run_regression.sh <label>
set -u
R=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
export ANDROID_SERIAL=${ONN_BOX:?set ONN_BOX to the box adb address}
L=${1:?label}
O=${REG_OUT:-/tmp/dvfel-regression}/$L; mkdir -p "$O"
for f in session_test_device.sh rate_play_device.sh stop_flash_device.sh snow_check_device.sh box_lock_device.sh; do
  adb push "$R/tests/$f" /data/local/tmp/ >/dev/null
done
F=/sdcard/Android/data/org.dvfel.dvfptest/files
sh_box() { adb shell "su -c '$1'"; }
echo "== version $(sh_box 'cat /sys/module/dvfel/version')"

echo "== 1. woman FEL, 60 Hz"
sh_box "sh /data/local/tmp/box_lock_device.sh take dvfel 240 >/dev/null && mv $F/woman.au $F/test.au && sh /data/local/tmp/session_test_device.sh >/dev/null 2>&1; mv $F/test.au $F/woman.au; sh /data/local/tmp/box_lock_device.sh release dvfel >/dev/null; grep -E \"fel_composer|bl paired\" /sys/kernel/debug/dvfel/stats"
rm -rf "$O/sess"; adb pull /sdcard/sess "$O/sess" >/dev/null
python3 -I "$R/tests/feldet.py" "$O"/sess/cap_*.bmp

echo "== 2. Grinch FEL, 2160p24"
sh_box "sh /data/local/tmp/rate_play_device.sh reg24 24 1 45 grinch | head -1; cmd display clear-user-preferred-display-mode"

echo "== 3. stop with Back, then kill"
sh_box "sh /data/local/tmp/stop_flash_device.sh reg_back back 3 | grep -vE \"^post\""
sh_box "sh /data/local/tmp/stop_flash_device.sh reg_kill kill 3 | grep -vE \"^post\""

echo "== 4. no-FEL play after the kill (green snow)"
sh_box "sh /data/local/tmp/snow_check_device.sh reg_snow 0 | grep -E \"draining\" | tail -1"
rm -rf "$O/snow"; adb pull /sdcard/snow/reg_snow "$O/snow" >/dev/null
for f in "$O"/snow/cap*.bmp; do echo "$(basename "$f") $(magick "$f" -format '%[fx:round(255*mean.r)],%[fx:round(255*mean.g)],%[fx:round(255*mean.b)]' info:)"; done
echo "== faults $(sh_box 'dmesg | grep -ciE "Oops|kernel BUG|Unable to handle|CFI failure|kernel panic"')"
