#!/usr/bin/env bash
# Play the clip with composition on, on again, then off, and compare output checksums.
P=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
OUT=$P/tests/logs/crc-series; mkdir -p "$OUT"
for spec in on1:composer=1 on2:composer=1 off:composer=0; do
  name=${spec%%:*}; args=${spec#*:}
  echo "##### run $name ($args)"
  CERT=1 DVFEL_ARGS=$args bash "$P/tests/run_play_test.sh" > "$OUT/$name.txt" 2>&1
  L=$(ls -d "$P"/tests/logs/play-* | tail -1)
  cp "$L/crc.txt" "$OUT/$name.crc"; cp "$L/regs-final.txt" "$OUT/$name.regs"; cp "$L/stats.txt" "$OUT/$name.stats"; cp "$L/dmesg.txt" "$OUT/$name.dmesg"
  grep -E 'done: |--- faults' "$OUT/$name.txt" | cut -c1-150
done
python3 -I "$P/tests/compare_composer.py" on1="$OUT/on1.dmesg" on2="$OUT/on2.dmesg" off="$OUT/off.dmesg"
