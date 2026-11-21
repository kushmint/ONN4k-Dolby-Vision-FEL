#!/system/bin/sh
# Shared test-box lock between Claude sessions. Usage:
#   box_lock_device.sh take <name> <seconds>   exit 0 = taken, 9 = held by someone else (prints who/until)
#   box_lock_device.sh release <name>          removes the lock only if <name> holds it
#   box_lock_device.sh show
L=/data/local/tmp/box.lock
now=$(date +%s)
case "$1" in
  take)
    if [ -f $L ]; then
      set -- "$1" "$2" "$3" $(cat $L)
      if [ "$4" != "$2" ] && [ "${5:-0}" -gt "$now" ]; then echo "BUSY $4 until $5 ($(( $5 - now )) s)"; exit 9; fi
    fi
    echo "$2 $(( now + $3 ))" > $L.tmp && mv $L.tmp $L && echo "TAKEN $2 until $(( now + $3 ))";;
  release)
    [ -f $L ] && [ "$(cut -d' ' -f1 $L)" = "$2" ] && rm -f $L; echo released;;
  show) cat $L 2>/dev/null || echo free; echo "now $now";;
esac
