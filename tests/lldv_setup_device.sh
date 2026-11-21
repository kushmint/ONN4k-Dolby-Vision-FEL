#!/system/bin/sh
# Memory-only LLDV output: a forced Dolby Vision V2 low-latency sink at a chosen
# peak, sent over a real 4:2:2 12-bit link and signalled to the display as HDR10.
# This matches avdvplus "Player Led (DV as HDR10 422 12 Bit)".
#
# Usage: lldv_setup_device.sh <nits>   enable (320 372 499 578 775 1037)
#        lldv_setup_device.sh check    report whether this display can do it, change nothing
#        lldv_setup_device.sh restore  put the normal output back
#        lldv_setup_device.sh engine_off [policy]  Dolby Vision engine off via bypass
#
# This script never wakes the box. Waking can switch the TV or AVR input over CEC.
P=/sys/module/aml_media/parameters
H=/sys/class/amhdmitx/amhdmitx0
M=/sys/module/dvfel/parameters
SAVED=/data/local/tmp/lldv.saved
WANT=422,12bit
ARG=${1:-499}

# The driver ends these lines with \n\r, so strip the \r before matching.
link_out() {
  tr -d '\r' < $H/hdmitx_basic_config | grep -i '^attr out:'
}

link_is() {
  link_out | grep -qi "$1"
}

# 4:2:2 is the only low-latency format the stock DRM driver allows, and HDMI
# carries it as 12-bit, so a separate "4:2:2 10-bit" fallback does not exist.
# A display without it stays on the HDR10 base layer.
preflight() {
  MODE=$(cat /sys/class/display/mode)
  echo "mode=$MODE attr=$(tr -d ' \r\n' < $H/attr)"
  grep -qx "$WANT" $H/dc_cap || { echo "UNSUPPORTED: display does not accept $WANT, keep the HDR10 base layer"; return 20; }
  grep -q 'SMPTE ST 2084: 1' $H/hdr_cap || { echo "UNSUPPORTED: display does not accept HDR10, keep the normal output"; return 21; }
  [ "$MODE" != dummy_l ] || { echo "ASLEEP: wake the box first"; return 12; }
  case "$MODE" in
    2160p*hz) W=3840; V=2160; RATE=${MODE#2160p};;
    1080p*hz) W=1920; V=1080; RATE=${MODE#1080p};;
    *) echo "UNSUPPORTED: output mode $MODE"; return 13;;
  esac
  RATE=${RATE%hz}
  TEMP=50; [ "$RATE" = 50 ] && TEMP=60
  grep -qx "${V}p${TEMP}hz" $H/disp_cap || { echo "UNSUPPORTED: no second mode to switch through"; return 14; }
  return 0
}

# dvfel holds the format on every mode change while lldv_force_422 is set. Android
# makes the change, and clearing the preference returns it to its default mode.
set_link() {
  echo "$1" > $M/lldv_force_422 || return 1
  cmd display set-user-preferred-display-mode "$W" "$V" "$TEMP" >/dev/null 2>&1
  sleep 6
  cmd display clear-user-preferred-display-mode >/dev/null 2>&1
  sleep 7
  [ "$(cat /sys/class/display/mode)" = "$MODE" ]
}

# Switch the Dolby Vision engine off cleanly. Writing dolby_vision_enable=N while the
# core is in an LLDV / HDR10 output mode freezes it there: the UI keeps going out
# PQ-encoded on an SDR link (washed out). Force bypass first, wait for it, then
# disable. $1 = policy to leave behind (stock idle: 1).
engine_off() {
  [ "$(cat $P/dolby_vision_enable)" = N ] && [ "$(cat $P/dolby_vision_status)" = 0 ] &&
    [ "$(cat $P/dolby_vision_mode)" = 5 ] && { echo "${1:-1}" > $P/dolby_vision_policy; return 0; }
  echo Y > $P/dolby_vision_enable
  echo 5 > $P/dolby_vision_mode
  echo 2 > $P/dolby_vision_policy
  n=0
  while [ $n -lt 20 ] && [ "$(cat $P/dolby_vision_status)" != 0 ]; do sleep 0.2; n=$((n + 1)); done
  echo N > $P/dolby_vision_enable
  echo "${1:-1}" > $P/dolby_vision_policy
}

restore_all() {
  LL=0; OUT=; EN=Y; POL=0
  [ -f $SAVED ] && . $SAVED
  [ -e $M/lldv_hdr10 ] && echo N > $M/lldv_hdr10
  echo "$LL" > $P/dolby_vision_ll_policy
  [ -n "$OUT" ] && echo "$OUT" > $P/dv_ll_output_mode
  echo 0 > $P/force_vsvdb
  X=$(tr -d ' \r\n' < $H/rawedid)
  echo "load $X" > $H/edid
  sleep 1
  preflight >/dev/null && [ -e $M/lldv_force_422 ] && set_link N
  # Engine state last: the mode changes above can switch it on or off again.
  if [ "$EN" = N ]; then engine_off "$POL"
  else echo Y > $P/dolby_vision_enable; echo "$POL" > $P/dolby_vision_policy; fi
  rm -f $SAVED
  echo "restored force_vsvdb=$(cat $P/force_vsvdb) ll=$(cat $P/dolby_vision_ll_policy) dv=$(cat $P/dolby_vision_enable)/$(cat $P/dolby_vision_mode) attr=$(tr -d ' \r\n' < $H/attr)"
}

case "$ARG" in
  check)
    preflight; RC=$?
    [ $RC = 0 ] && echo "SUPPORTED: $WANT at $MODE"
    exit $RC;;
  restore)
    restore_all
    exit 0;;
  engine_off)
    engine_off "${2:-1}"
    exit 0;;
  320) STEP=8;; 372) STEP=9;; 499) STEP=11;; 578) STEP=12;; 775) STEP=14;; 1037) STEP=16;;
  *) echo "unsupported nits $ARG"; exit 2;;
esac

preflight || exit $?
[ -e $M/lldv_hdr10 ] || { echo "dvfel is not loaded"; exit 8; }

echo "LL=$(cat $P/dolby_vision_ll_policy)" > $SAVED
echo "OUT=$(cat $P/dv_ll_output_mode)" >> $SAVED
echo "EN=$(cat $P/dolby_vision_enable)" >> $SAVED
echo "POL=$(cat $P/dolby_vision_policy)" >> $SAVED

# target_max_pq_v2 = (PQ12(nits) - 2055) / 65
echo 235,1,70,208,0,69,11,$((STEP << 3)),134,96,118,143 > $P/vsvdb_data || exit 3
echo 5 > $P/force_vsvdb || exit 4
X=$(tr -d ' \r\n' < $H/rawedid)
echo "load $X" > $H/edid || exit 5
sleep 1
if ! grep -q LL_YCbCr_422_12BIT $H/dv_cap; then
  echo "FAILED: forced sink does not report low-latency 4:2:2 12-bit"
  restore_all
  exit 6
fi

# EARLY=1 (playback already started): put the Dolby Vision engine into LLDV before the
# link change, so the library sets up FEL for LLDV instead of the stock path.
if [ -n "${EARLY:-}" ]; then
  echo "$ARG" > $M/lldv_peak_nits; echo Y > $M/lldv_hdr10
  echo 1 > $P/dolby_vision_ll_policy; echo 2 > $P/dv_ll_output_mode
  echo Y > $P/dolby_vision_enable; echo 0 > $P/dolby_vision_policy
fi

set_link Y
if ! link_is "$WANT"; then
  echo "FAILED: link is $(link_out), not $WANT"
  restore_all
  exit 10
fi

# Only signal LLDV once the link really carries it.
# The mode change can switch the Dolby Vision engine off or force it to SDR.
echo Y > $P/dolby_vision_enable || exit 7
echo 0 > $P/dolby_vision_policy || exit 7
echo 1 > $P/dolby_vision_ll_policy || exit 7
echo 2 > $P/dv_ll_output_mode || exit 7
echo "$ARG" > $M/lldv_peak_nits || exit 9
echo Y > $M/lldv_hdr10 || exit 9

echo "lldv nits=$ARG step=$STEP vsvdb=$(cat $P/vsvdb_data)"
grep -E "VSVDB:|LL_" $H/dv_cap
link_out
echo "LLDV_READY ll=$(cat $P/dolby_vision_ll_policy) out=$(cat $P/dv_ll_output_mode) attr=$(tr -d ' \r\n' < $H/attr)"
