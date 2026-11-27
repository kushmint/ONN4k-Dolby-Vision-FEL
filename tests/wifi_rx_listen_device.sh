# The box listens on 9001; feed it from the PC with: dd if=/dev/zero bs=1M count=3000 | socat -u - TCP:<box>:9001 (the PC firewall blocks inbound). Prints MB received.
rx0=$(awk '/wlan0/{print $2}' /proc/net/dev); t0=$(cut -d. -f1 /proc/uptime)
timeout 14 nc -l -p 9001 > /dev/null &
sleep 2; echo ready
sleep 12
rx1=$(awk '/wlan0/{print $2}' /proc/net/dev); t1=$(cut -d. -f1 /proc/uptime)
echo "received $(( (rx1-rx0)/1000000 )) MB in $((t1-t0-2)) s = $(( (rx1-rx0)*8/1000000/(t1-t0-2) )) Mbit/s"
