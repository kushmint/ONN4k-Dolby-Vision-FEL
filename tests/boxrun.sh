#!/bin/bash
# boxrun.sh <seconds> <device shell command...>: wait for the box lock (name dvfel), run the
# command as root on .141 while holding it, then release. Inner scripts that take the lock
# themselves see "dvfel" and proceed.
export ANDROID_SERIAL=${ONN_BOX:?set ONN_BOX}
T=${1:?seconds}; shift
for i in $(seq 1 120); do
  out=$(adb shell "su -c 'sh /data/local/tmp/box_lock_device.sh take dvfel $T'" 2>&1)
  echo "$out" | grep -q TAKEN && break
  [ $i = 1 ] && echo "waiting: $out"
  sleep 10
done
echo "$out" | grep -q TAKEN || { echo "no lock: $out"; exit 9; }
adb shell "su -c '$*'"
rc=$?
adb shell "su -c 'sh /data/local/tmp/box_lock_device.sh release dvfel'" >/dev/null
exit $rc
