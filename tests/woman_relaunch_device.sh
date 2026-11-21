#!/system/bin/sh
# Replays the test clip by relaunching the player, for about three hours.
PKG=org.dvfel.dvfptest
setprop debug.dvfp.test.loops 1
i=0
while [ $i -lt 85 ]; do
  input keyevent KEYCODE_WAKEUP
  am force-stop $PKG
  sleep 2
  am start -n $PKG/android.app.NativeActivity >/dev/null 2>&1
  sleep 126
  i=$((i + 1))
done
am force-stop $PKG
