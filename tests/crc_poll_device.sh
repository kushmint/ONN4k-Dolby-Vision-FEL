#!/system/bin/sh
# Sample the driver's per-frame composer checksums into the kernel log for $1 seconds.
# With $2=1, enable the driver's certification-mode checksums for the duration.
d=/sys/class/amdolby_vision/debug
p=/sys/module/aml_media/parameters/dolby_vision_flags
q=/sys/module/aml_media/parameters/debug_dolby
[ "${2:-0}" = 1 ] || exit 0
echo amdv_crc 1 > $d
v=$(cat $p); echo $((v | 64)) > $p
o=$(cat $q); echo $((o | 65536)) > $q
end=$(( $(date +%s) + ${1:-70} ))
while [ "$(date +%s)" -lt "$end" ]; do
  echo amdv_crc 0 > $d
  sleep 0.03
done
echo "$o" > $q
v=$(cat $p); echo $((v & ~64)) > $p
