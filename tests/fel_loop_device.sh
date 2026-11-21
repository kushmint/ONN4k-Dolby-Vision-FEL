#!/system/bin/sh
# Plays the test clip 4 times, saving regs, stats and 6 captures at 84 s per play to /sdcard/loop.
PKG=org.dvfel.dvfptest; DBG=/sys/kernel/debug/dvfel; O=/sdcard/loop; rm -rf $O; mkdir -p $O
k=0; while [ $k -lt 4 ]; do k=$((k+1))
 am force-stop $PKG; sleep 2; logcat -c; dmesg -c >/dev/null
 am start -n $PKG/android.app.NativeActivity >/dev/null 2>&1; T0=$(cut -d. -f1 /proc/uptime)
 sleep 15; dmesg > $O/p${k}_dmesg_start.txt; cat $DBG/regs > $O/p${k}_regs_t15.txt
 sleep 69; cat $DBG/regs > $O/p${k}_regs_t84.txt; cat $DBG/stats > $O/p${k}_stats_t84.txt
 n=0; while [ $n -lt 6 ]; do n=$((n+1)); cd /data/temp; /vendor/bin/screencatch -m -t 1 -p /data/temp 0 0 3840 2160 960 540 >/dev/null 2>&1; mv /data/temp/1.bmp $O/p${k}_cap$n.bmp; done
 i=0; while [ $i -lt 30 ]; do logcat -d | grep -q 'dvfp_test: done:' && break; sleep 2; i=$((i+1)); done
 logcat -d | grep -E 'dvfp_test|dvfelplay' > $O/p${k}_logcat.txt; dmesg > $O/p${k}_dmesg_end.txt
 echo "play $k done t=$(( $(cut -d. -f1 /proc/uptime) - T0 ))s"
done
