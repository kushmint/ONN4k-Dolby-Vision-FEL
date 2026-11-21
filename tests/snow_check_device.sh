#!/system/bin/sh
# snow_device.sh <label> <fel 0|1>: dvfptest Grinch play, VDIN captures at ~20 s, stats. Uses the box lock.
PKG=org.dvfel.dvfptest; F=/sdcard/Android/data/$PKG/files; O=/sdcard/snow/$1; LOCK=/data/local/tmp/box.lock
L=$1; FEL=$2
if [ -z "$BOXHELD" ]; then sh /data/local/tmp/box_lock_device.sh take dvfel 150 >/dev/null || { echo BUSY; exit 9; }; fi
rm -rf $O; mkdir -p $O /data/temp; chmod 777 /data/temp
grep -E "^fixes|draining|^version|on_display" /sys/kernel/debug/dvfel/stats > $O/before.txt
mv $F/grinch.au $F/test.au
setprop debug.dvfp.test.fel $FEL; setprop debug.dvfp.test.loops 1; setprop debug.dvfp.test.codec ${CODEC:-c2.amlogic.dolby-vision.dvhe.decoder}
am force-stop $PKG; sleep 1; logcat -c
am start -n $PKG/android.app.NativeActivity >/dev/null 2>&1; sleep 20
n=0; while [ $n -lt 3 ]; do n=$((n+1)); cd /data/temp; /vendor/bin/screencatch -m -t 1 -p /data/temp 0 0 3840 2160 960 540 >/dev/null 2>&1; mv /data/temp/1.bmp $O/cap$n.bmp; sleep 1; done
grep -E "^fixes|draining|^version|on_display|bl paired|composer" /sys/kernel/debug/dvfel/stats > $O/during.txt
grep -E "live 01000032|cur_el_flag" /sys/kernel/debug/dvfel/regs >> $O/during.txt
cat /sys/kernel/debug/dvfel/regs > $O/regs_full.txt; cat /sys/kernel/debug/dvfel/stats > $O/stats_full.txt
for p in /sys/module/aml_media/parameters/*dolby* /sys/module/aml_media/parameters/*amdv* /sys/module/aml_media/parameters/dv_* /sys/module/aml_media/parameters/*el*; do echo "$(basename $p)=$(cat $p 2>/dev/null | cut -c1-120)"; done > $O/params.txt 2>/dev/null
cat /sys/class/amdolby_vision/dv_mode > $O/dvmode.txt 2>/dev/null; cat /sys/class/video/video_state > $O/video_state.txt 2>/dev/null; cat /sys/class/vfm/map > $O/vfm_map.txt 2>/dev/null
logcat -d | grep -E "dvfp_test|dvfelplay" | grep -iE "fel|session|8\.1|dvfel|error" | head -8 > $O/log.txt
am force-stop $PKG; sleep 2; mv $F/test.au $F/grinch.au
[ -z "$BOXHELD" ] && sh /data/local/tmp/box_lock_device.sh release dvfel >/dev/null
cat $O/before.txt; echo ---; cat $O/during.txt; echo ---; cat $O/log.txt
