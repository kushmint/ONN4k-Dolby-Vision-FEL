#!/system/bin/sh
# One dvfptest play at a chosen refresh rate, counting display-side frame drops.
# Usage: rate_play_device.sh <label> <hz: 23.976|24|25|30|50|60> <fel 0|1> [duration_s, default 45] [clip: grinch|woman, default grinch]
# dvfptest plays files/test.au; the clips sit beside it as grinch.au / woman.au and are renamed in for the play.
# Android only switches with the exact refresh value of a supported mode (24 -> 24.000002).
# Takes /data/local/tmp/box.lock while it runs. Output: /sdcard/rate/<label>.
PKG=org.dvfel.dvfptest; DBG=/sys/kernel/debug/dvfel; VC=/sys/class/video_composer
LABEL=$1; O=/sdcard/rate/$1; HZ=$2; FEL=$3; DUR=${4:-45}; CLIP=${5:-grinch}; LOCK=/data/local/tmp/box.lock
F=/sdcard/Android/data/$PKG/files
now() { cut -d. -f1 /proc/uptime; }
if [ -z "$BOXHELD" ]; then sh /data/local/tmp/box_lock_device.sh take dvfel $(( DUR + 180 )) >/dev/null || { echo BUSY; exit 9; }; fi
[ -f $F/$CLIP.au ] || { echo "no clip $F/$CLIP.au"; [ -z "$BOXHELD" ] && sh /data/local/tmp/box_lock_device.sh release dvfel >/dev/null; exit 8; }
mv $F/$CLIP.au $F/test.au
rm -rf $O; mkdir -p $O
vc() { for f in receive_count drop_cnt total_get_count; do echo "$f $(cat $VC/$f)"; done; echo "mode $(cat /sys/class/display/mode)"; }
case $HZ in 23.976) R=23.976025;; 24) R=24.000002;; 25) R=25.0;; 30) R=30.000002;; 50) R=50.0;; 60) R=60.000004;; *) R=$HZ;; esac
cmd display set-user-preferred-display-mode 3840 2160 $R >/dev/null 2>&1; sleep 8
echo "mode=$(cat /sys/class/display/mode) attr=$(tr -d ' \r\n' < /sys/class/amhdmitx/amhdmitx0/attr)" > $O/state.txt
setprop debug.dvfp.test.fel $FEL; setprop debug.dvfp.test.loops 1; setprop debug.dvfp.test.codec c2.amlogic.dolby-vision.dvhe.decoder
am force-stop $PKG; sleep 2; logcat -c
am start -n $PKG/android.app.NativeActivity >/dev/null 2>&1; T0=$(now)
while [ $(( $(now) - T0 )) -lt 15 ]; do sleep 0.5; done
cat $DBG/stats > $O/stats_a.txt; vc > $O/vc_a.txt
while [ $(( $(now) - T0 )) -lt $DUR ]; do sleep 0.5; done
cat $DBG/stats > $O/stats_b.txt; vc > $O/vc_b.txt; cat $DBG/regs > $O/regs_b.txt
logcat -d | grep -E 'dvfp_test|dvfelplay' > $O/logcat.txt
am force-stop $PKG; sleep 2; mv $F/test.au $F/$CLIP.au
cmd display clear-user-preferred-display-mode >/dev/null 2>&1; sleep 8
echo "back=$(cat /sys/class/display/mode) attr=$(tr -d ' \r\n' < /sys/class/amhdmitx/amhdmitx0/attr)" >> $O/state.txt
[ -z "$BOXHELD" ] && sh /data/local/tmp/box_lock_device.sh release dvfel >/dev/null
# per-play deltas (15 s to DUR)
d() { a=$(grep "$1" $O/$2_a.txt | grep -oE "$3[: ]+[0-9]+" | grep -oE '[0-9]+$'); b=$(grep "$1" $O/$2_b.txt | grep -oE "$3[: ]+[0-9]+" | grep -oE '[0-9]+$'); echo $(( ${b:-0} - ${a:-0} )); }
SEC=$((DUR - 15))
echo "$LABEL clip=$CLIP hz=$HZ fel=$FEL over ${SEC}s: vc_received=$(d drop_cnt vc rec_cnt) vc_frames=$(d drop_cnt vc frame_index) vc_player_drop=$(d drop_cnt vc player_drop_cnt) paired=$(d 'bl paired' stats 'bl paired') no_el=$(d 'bl paired' stats no_el) el_dropped=$(d '^el in' stats dropped) wait_checks=$(d '^wait' stats checks) wait_timeouts=$(d '^wait' stats timeouts)"
cat $O/state.txt
echo faults=$(dmesg | grep -ciE 'Oops|kernel BUG|Unable to handle|CFI failure|kernel panic')
