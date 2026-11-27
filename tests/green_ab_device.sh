# A/B of the clear_stale_el module param: 25 rounds of (FEL play, stop, plain play, green count). Push green_count_device.sh to /data/local/tmp/gcount.sh first. Run alone: two play scripts at once swap the test clips.
PKG=org.dvfel.dvfptest; F=/sdcard/Android/data/$PKG/files; P=/sys/module/dvfel/parameters/clear_stale_el
play() { input keyevent KEYCODE_WAKEUP; [ -f $F/grinch.au ] && mv $F/grinch.au $F/test.au; setprop debug.dvfp.test.fel $1; setprop debug.dvfp.test.loops 1; setprop debug.dvfp.test.codec c2.amlogic.dolby-vision.dvhe.decoder
  am force-stop $PKG; sleep 1; am start -n $PKG/android.app.NativeActivity >/dev/null 2>&1; }
stopp() { am force-stop $PKG; sleep 2; [ -f $F/test.au ] && mv $F/test.au $F/grinch.au; }
stopp
for round in $(seq 1 $1); do
 for arm in N Y; do
  echo $arm > $P; out=""
  play 1; sleep 8; stopp; sleep 1; play 0; sleep 6; c=$(sh /data/local/tmp/gcount.sh); stopp
  echo "arm clear_stale_el=$arm: $c"
 done
done
