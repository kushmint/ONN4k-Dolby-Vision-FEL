# Debug build only (needs the clrel debugfs file from diag/diag-2026-11-24b.patch): clears the stock EL pointer in a green play.
PKG=org.dvfel.dvfptest; F=/sdcard/Android/data/$PKG/files; K=/sys/kernel/debug/dvfel/clrel
play() { input keyevent KEYCODE_WAKEUP; [ -f $F/grinch.au ] && mv $F/grinch.au $F/test.au; setprop debug.dvfp.test.fel $1; setprop debug.dvfp.test.loops 1; setprop debug.dvfp.test.codec c2.amlogic.dolby-vision.dvhe.decoder
  am force-stop $PKG; sleep 1; am start -n $PKG/android.app.NativeActivity >/dev/null 2>&1; }
stopp() { am force-stop $PKG; sleep 2; [ -f $F/test.au ] && mv $F/test.au $F/grinch.au; }
stopp
for n in $(seq 1 $1); do
  play 1; sleep 8; stopp; play 0; sleep 6; c=$(sh /data/local/tmp/gcount.sh); v=$(cat $K)
  line="round $n green=$c $v"
  if [ "$c" -ge 5 ]; then echo 1 > $K; sleep 2; c2=$(sh /data/local/tmp/gcount.sh); line="$line | after clear green=$c2 $(cat $K)"; fi
  echo "$line"; stopp
done
