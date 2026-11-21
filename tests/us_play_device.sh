#!/system/bin/sh
# One display-led play (for a Dolby Vision display). Usage: us_play_device.sh <label> <fel 0|1>
# Saves state, regs, stats and 6 VDIN1 captures at 84 s to /sdcard/us/<label>.
PKG=org.dvfel.dvfptest; DBG=/sys/kernel/debug/dvfel; O=/sdcard/us/$1; rm -rf $O; mkdir -p $O
H=/sys/class/amhdmitx/amhdmitx0; P=/sys/module/aml_media/parameters
st(){ echo "t=$(( $(cut -d. -f1 /proc/uptime) - T0 )) hdr=$(cat $H/hdmi_hdr_status) attr=$(tr -d ' \r\n' < $H/attr) out=$(tr -d '\r' < $H/hdmitx_basic_config | grep -i '^attr out:' | cut -d: -f2) en=$(cat $P/dolby_vision_enable) pol=$(cat $P/dolby_vision_policy) $(tail -1 /sys/class/amdolby_vision/dv_mode)"; }
setprop ctl.start screen_control; mkdir -p /data/temp; chmod 777 /data/temp
input keyevent KEYCODE_WAKEUP
setprop debug.dvfp.test.fel $2; setprop debug.dvfp.test.loops 1; setprop debug.dvfp.test.codec c2.amlogic.dolby-vision.dvhe.decoder
am force-stop $PKG; sleep 2; logcat -c; dmesg -c >/dev/null
am start -n $PKG/android.app.NativeActivity >/dev/null 2>&1; T0=$(cut -d. -f1 /proc/uptime)
sleep 15; st >> $O/state.txt; cat $DBG/regs > $O/regs_t15.txt
sleep 69; st >> $O/state.txt; cat $DBG/regs > $O/regs_t84.txt; cat $DBG/stats > $O/stats_t84.txt
n=0; while [ $n -lt 6 ]; do n=$((n+1)); cd /data/temp; /vendor/bin/screencatch -m -t 1 -p /data/temp 0 0 3840 2160 960 540 >/dev/null 2>&1; mv /data/temp/1.bmp $O/cap$n.bmp; done
cat $H/hdmitx_pkt_dump > $O/pkt.txt 2>/dev/null
i=0; while [ $i -lt 30 ]; do logcat -d | grep -q 'dvfp_test: done:' && break; sleep 2; i=$((i+1)); done
logcat -d | grep -E 'dvfp_test|dvfelplay' > $O/logcat.txt; dmesg > $O/dmesg.txt
am force-stop $PKG; sleep 4; st >> $O/state.txt
echo "faults=$(grep -ciE 'Oops|BUG:|Unable to handle|CFI failure|kernel panic|WARNING: CPU|Internal error' $O/dmesg.txt)" >> $O/state.txt
cat $O/state.txt
