#!/system/bin/sh
# Loads dvfel after boot, applies the saved output settings, then watches the display
# so the settings survive sleep and wake. Everything stays in memory.
MODDIR=${0%/*}
DATA=/data/adb/onn_dvfel
LOG=$DATA/log.txt
mkdir -p $DATA
[ -f $DATA/settings.conf ] || cp $MODDIR/settings.conf $DATA/settings.conf
log() { echo "$(date '+%m-%d %H:%M:%S') $*" >> $LOG; }
[ -f $LOG ] && [ "$(wc -c < $LOG)" -gt 200000 ] && mv $LOG $LOG.old

until [ "$(getprop sys.boot_completed)" = 1 ]; do sleep 2; done
sleep 5

FW=$(getprop ro.build.version.incremental)
case "$FW" in
  15016191|15640252) ;;
  *) log "unsupported firmware $FW, not loading"; exit 0;;
esac

mountpoint -q /sys/kernel/debug || mount -t debugfs none /sys/kernel/debug
if ! grep -q '^dvfel ' /proc/modules; then
  if insmod $MODDIR/dvfel.ko open_driver_fixed=1 fel_composer_mode=1; then
    log "dvfel loaded ($FW)"
    # this build works on this box: keep it as the fallback
    cmp -s $MODDIR/dvfel.ko $MODDIR/dvfel-good.ko || cp $MODDIR/dvfel.ko $MODDIR/dvfel-good.ko
  else
    log "dvfel refused to load ($FW): $(dmesg | grep -i -E 'dvfel|disagrees|unknown symbol' | tail -3 | tr '\n' ' ')"
    # a test build that does not load: fall back to the last build that did
    if [ -f $MODDIR/dvfel-good.ko ] && insmod $MODDIR/dvfel-good.ko open_driver_fixed=1 fel_composer_mode=1; then
      log "dvfel loaded the fallback dvfel-good.ko ($(cat /sys/module/dvfel/version 2>/dev/null))"
    else
      exit 0
    fi
  fi
fi
i=0
while [ ! -e /dev/dvfel ] && [ $i -lt 50 ]; do sleep 0.1; i=$((i + 1)); done
# As signde's Fire TV module: a base-layer frame may wait briefly for its EL
# (Kodi's EL arrives later than VLC's), longer right after a seek.
echo 1 > /sys/module/dvfel/parameters/wait_el
echo 200 > /sys/module/dvfel/parameters/seek_wait_ms
magiskpolicy --live \
  "type dvfel_device" "typeattribute dvfel_device dev_type" \
  "typeattribute dvfel_device mlstrustedobject" \
  "type dvfel_sysfs" "typeattribute dvfel_sysfs fs_type" "typeattribute dvfel_sysfs sysfs_type" \
  "allow untrusted_app_all dvfel_device chr_file { getattr open read write ioctl }" \
  "allow untrusted_app_all dvfel_sysfs file { getattr open read }" \
  "allow untrusted_app_all sysfs dir { search getattr }"
chmod 666 /dev/dvfel
chcon u:object_r:dvfel_device:s0 /dev/dvfel
chcon u:object_r:dvfel_sysfs:s0 /sys/module/dvfel/version

# Remember the stock engine state so the home screen goes back to exactly that.
cat /sys/module/aml_media/parameters/dolby_vision_enable > $DATA/enable_boot
# The badge needs to draw over other apps or post a toast.
pm grant org.dvfel.settings android.permission.POST_NOTIFICATIONS >> $LOG 2>&1
appops set org.dvfel.settings SYSTEM_ALERT_WINDOW allow >> $LOG 2>&1
log "badge permissions: $(appops get org.dvfel.settings SYSTEM_ALERT_WINDOW 2>&1) notifications=$(dumpsys package org.dvfel.settings | grep -c 'POST_NOTIFICATIONS: granted=true')"
sh $MODDIR/dvfelctl apply >> $LOG 2>&1
sh $MODDIR/dvfelctl monitor >> $LOG 2>&1 &
