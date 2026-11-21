#!/system/bin/sh
# Prints readable video and Dolby Vision driver state, one value per line.
for d in /sys/class/video /sys/class/amdolby_vision /sys/class/video_composer /sys/module/amvideo/parameters /sys/module/amdolby_vision/parameters /sys/module/aml_media/parameters; do
  [ -d "$d" ] || continue
  for f in "$d"/*; do
    [ -f "$f" ] && [ -r "$f" ] || continue
    case "$f" in *video_state|*vframe_states|*reg*|*debug*|*dump*|*crc*|*hist*) continue;; esac
    v=$(timeout 2 head -c 300 "$f" 2>/dev/null | tr '\n' ' ')
    echo "$f=$v"
  done
done
echo "vfm=$(cat /sys/class/vfm/map | tr '\n' ';')"
timeout 2 cat /sys/class/video/vframe_states | tr '\n' ' '; echo
