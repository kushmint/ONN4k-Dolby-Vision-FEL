# Plays a URL in VLC FEL / Kodi FEL and logs wlan0 Mbit/s, player drops, frames per 3 s, load every 2-3 s (see CLAUDE.md "SMB lag").
# usage: smbtrace.sh <vlc|kodi> <url> <label> <seconds>
P=$1; URL=$2; L=$3; N=${4:-90}
DBG=/sys/kernel/debug/dvfel; VC=/sys/class/video_composer; O=/sdcard/smbt/$L; rm -rf $O; mkdir -p $O
case $P in
 vlc) PKG=org.videolan.vlc.fel; CMP=$PKG/org.videolan.vlc.gui.video.VideoPlayerActivity;;
 kodi) PKG=org.xbmc.kodi.fel; CMP=;;
esac
echo ">> starting: $P playing $URL for $N s"
input keyevent KEYCODE_WAKEUP; am force-stop org.dvfel.dvfptest; am force-stop org.videolan.vlc.fel; am force-stop org.xbmc.kodi.fel; sleep 2
logcat -c; dmesg -c >/dev/null
if [ -n "$CMP" ]; then am start -n $CMP -a android.intent.action.VIEW -d "$URL" -t video/x-matroska >/dev/null
else am start -a android.intent.action.VIEW -d "$URL" -t video/x-matroska -p $PKG >/dev/null; fi
T0=$(cut -d. -f1 /proc/uptime); prx=0; pdrop=0; prec=0
echo "t rx_Mbps player_drop_delta rec_delta fps_info paired no_el load mode" > $O/trace.txt
while [ $(( $(cut -d. -f1 /proc/uptime) - T0 )) -lt $N ]; do
  sleep 2
  t=$(( $(cut -d. -f1 /proc/uptime) - T0 ))
  rx=$(awk '/wlan0/{print $2}' /proc/net/dev); dc=$(cat $VC/drop_cnt); drop=$(echo "$dc" | sed -n "s/.*player_drop_cnt: \([0-9]*\).*/\1/p"); rec=$(echo "$dc" | sed -n "s/.*rec_cnt: \([0-9]*\).*/\1/p"); vdrop=$(echo "$dc" | sed -n "s/.*vpp_drop_cnt: \([0-9]*\).*/\1/p"); drop=${drop:-0}; rec=${rec:-0}
  mbps=$(( (rx - prx) * 8 / 2000000 )); [ $prx = 0 ] && mbps=0
  echo "$t $mbps $((drop-pdrop)) $((rec-prec)) $(cat /sys/class/video/fps_info | tr ' ' '_') $(grep -E '^bl paired' $DBG/stats | tr ' ' '_') $(cut -d' ' -f1 /proc/loadavg) $(cat /sys/class/display/mode)" >> $O/trace.txt
  prx=$rx; pdrop=$drop; prec=$rec
done
cat $DBG/stats > $O/stats.txt; top -b -n 1 -m 12 > $O/top.txt 2>&1
logcat -d > $O/logcat.txt; dmesg > $O/dmesg.txt
am force-stop $PKG; sleep 2
echo "$P $L done"
