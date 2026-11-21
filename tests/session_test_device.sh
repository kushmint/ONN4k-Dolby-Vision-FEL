#!/system/bin/sh
# One play with the Magisk module managing LLDV: status timeline, badge screenshots,
# FEL captures at 85 s and the switch back after the clip. Output in /sdcard/sess.
PKG=org.dvfel.dvfptest; C="sh /data/adb/modules/onn_dvfel/dvfelctl"; O=/sdcard/sess
rm -rf $O; mkdir -p $O
st(){ echo "t=$(( $(cut -d. -f1 /proc/uptime) - T0 )) $($C status | grep -E '^(active|link|hdr|dv|fel)=' | tr '\n' ' ')"; }
at(){ while [ $(( $(cut -d. -f1 /proc/uptime) - T0 )) -lt $1 ]; do sleep 0.5; done; }
setprop ctl.start screen_control; mkdir -p /data/temp; chmod 777 /data/temp
setprop debug.dvfp.test.fel 1; setprop debug.dvfp.test.loops 1; setprop debug.dvfp.test.codec c2.amlogic.dolby-vision.dvhe.decoder
logcat -c
am start -n $PKG/android.app.NativeActivity >/dev/null 2>&1; T0=$(cut -d. -f1 /proc/uptime)
for t in 3 8 13 18; do at $t; st; done
n=0; while [ $n -lt 8 ]; do n=$((n+1)); at $((19 + n * 2)); screencap -p $O/ui_$n.png; done; st
at 85
n=0; while [ $n -lt 4 ]; do n=$((n+1)); cd /data/temp; /vendor/bin/screencatch -m -t 1 -p /data/temp 0 0 3840 2160 960 540 >/dev/null 2>&1; mv /data/temp/1.bmp $O/cap_$n.bmp; done; st
cat /sys/kernel/debug/dvfel/regs > $O/regs.txt
i=0; while [ $i -lt 40 ]; do logcat -d | grep -q 'dvfp_test: done:' && break; sleep 2; i=$((i+1)); done; st
am force-stop $PKG
for k in 1 2 3 4 5 6 7 8; do sleep 3; st; done
tail -8 /data/adb/onn_dvfel/log.txt
logcat -d | grep -iE 'Suppressing toast|BadgeReceiver|addView|overlay' | grep -i dvfel | tail -3 | cut -c1-150
echo faults=$(dmesg | grep -ciE 'Oops|BUG:|Unable to handle|CFI failure|kernel panic')
