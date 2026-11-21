#!/system/bin/sh
# stop_flash_device.sh <label> <back|kill> [n]: dvfptest Grinch FEL play, stopped at 12 s by the Back key or a
# force-stop, then n (default 8) VDIN captures of the video plane right after the stop. Uses the box lock.
PKG=org.dvfel.dvfptest; F=/sdcard/Android/data/$PKG/files; O=/sdcard/flash/$1; N=${3:-8}
if [ -z "$BOXHELD" ]; then sh /data/local/tmp/box_lock_device.sh take dvfel 120 >/dev/null || { echo BUSY; exit 9; }; fi
rm -rf $O; mkdir -p $O /data/temp; chmod 777 /data/temp
mv $F/grinch.au $F/test.au
setprop debug.dvfp.test.fel 1; setprop debug.dvfp.test.loops 1; setprop debug.dvfp.test.codec c2.amlogic.dolby-vision.dvhe.decoder
am force-stop $PKG; sleep 1
am start -n $PKG/android.app.NativeActivity >/dev/null 2>&1; sleep 12
cd /data/temp; /vendor/bin/screencatch -m -t 1 -p /data/temp 0 0 3840 2160 960 540 >/dev/null 2>&1; mv 1.bmp $O/pre.bmp
if [ "$2" = kill ]; then am force-stop $PKG; else input keyevent BACK; fi
T0=$(date +%s%N)
n=0; while [ $n -lt $N ]; do n=$((n+1)); /vendor/bin/screencatch -m -t 1 -p /data/temp 0 0 3840 2160 960 540 >/dev/null 2>&1; mv 1.bmp $O/post$n.bmp; echo "post$n $(( ($(date +%s%N) - T0) / 1000000 )) ms" >> $O/times.txt; done
sleep 2; am force-stop $PKG; mv $F/test.au $F/grinch.au
grep -E "^version|draining|restore" /sys/kernel/debug/dvfel/stats > $O/stats.txt
dmesg | grep -iE "rdma" | tail -3 >> $O/stats.txt
[ -z "$BOXHELD" ] && sh /data/local/tmp/box_lock_device.sh release dvfel >/dev/null
cat $O/times.txt $O/stats.txt
