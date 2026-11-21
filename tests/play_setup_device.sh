#!/system/bin/sh
# Runs as root on the test box. Everything here lives in memory until reboot.
R=/data/local/tmp
grep -q '^dvfel ' /proc/modules || insmod $R/dvfel.ko open_driver_fixed=1 debug=1 fel_composer_mode=1 trace_regs=1 $DVFEL_ARGS || exit 10
i=0
while [ ! -e /dev/dvfel ] && [ $i -lt 50 ]; do sleep 0.1; i=$((i + 1)); done
[ -e /dev/dvfel ] || exit 11
magiskpolicy --live \
  "type dvfel_device" "typeattribute dvfel_device dev_type" \
  "typeattribute dvfel_device mlstrustedobject" \
  "type dvfel_sysfs" "typeattribute dvfel_sysfs fs_type" "typeattribute dvfel_sysfs sysfs_type" \
  "allow untrusted_app_all dvfel_device chr_file { getattr open read write ioctl }" \
  "allow untrusted_app_all dvfel_sysfs file { getattr open read }" \
  "allow untrusted_app_all sysfs dir { search getattr }" || exit 12
chmod 666 /dev/dvfel
chcon u:object_r:dvfel_device:s0 /dev/dvfel || exit 13
chcon u:object_r:dvfel_sysfs:s0 /sys/module/dvfel/version || exit 14
mountpoint -q /sys/kernel/debug || mount -t debugfs none /sys/kernel/debug
ls -lZ /dev/dvfel /sys/module/dvfel/version
echo setup_ok
