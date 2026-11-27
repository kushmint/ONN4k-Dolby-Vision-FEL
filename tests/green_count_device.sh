# Prints how many of 9 VDIN sample pixels look green (G > R+30 and G > B+30); >= 5 = green snow. Needs the app playing.
# prints number of green-looking sample pixels out of 9 (3 captures x 3 points)
mkdir -p /data/temp; chmod 777 /data/temp; cd /data/temp; g=0
for k in 1 2 3; do rm -f 1.bmp; /vendor/bin/screencatch -m -t 1 -p /data/temp 0 0 3840 2160 960 540 >/dev/null 2>&1
  off=$(od -An -tu4 -j10 -N4 1.bmp | tr -d ' ')
  for xy in "480 270" "200 150" "760 400"; do set -- $xy; p=$((off + ($2*960+$1)*4)); set -- $(od -An -tu1 -j$p -N3 1.bmp); b=$1; gg=$2; r=$3
    if [ $gg -gt $((r+30)) ] && [ $gg -gt $((b+30)) ]; then g=$((g+1)); fi; done
  sleep 1; done
echo $g
