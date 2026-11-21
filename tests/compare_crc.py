#!/usr/bin/env python3
"""Compare Dolby Vision output checksums between playback runs of the same clip."""
import sys

runs = [(a.split("=", 1)[0], a.split("=", 1)[1]) for a in sys.argv[1:]]
data = {}
for name, path in runs:
    data[name] = {line.strip() for line in open(path) if line.strip()}
    print(f"{name}: {len(data[name])} unique output checksums")
names = list(data)
for i, a in enumerate(names):
    for b in names[i + 1:]:
        common = len(data[a] & data[b])
        print(f"{a} vs {b}: {common} shared ({100 * common / max(min(len(data[a]), len(data[b])), 1):.1f}% of the smaller run)")
