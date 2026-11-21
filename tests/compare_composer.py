#!/usr/bin/env python3
"""Compare Dolby Vision core3 input checksums for identical base layer frames across runs.

The core3 input carries the composed picture. The base layer checksum identifies the
frame, so two runs should agree when composition matches and differ when it does not.
"""
import collections, re, sys

PAT = re.compile(r"core1 bl crc 0x([0-9a-f]+),dm 0x([0-9a-f]+),core3 in 0x([0-9a-f]+),out 0x([0-9a-f]+)")


def load(path):
    frames = collections.defaultdict(collections.Counter)
    for line in open(path, errors="replace"):
        m = PAT.search(line)
        if m and m[1] != "0" and m[3] != "0":
            frames[m[1]][m[3]] += 1
    return frames


def agree(a, b):
    shared = set(a) & set(b)
    exact = sum(1 for k in shared if set(a[k]) & set(b[k]))
    return len(shared), exact


runs = {x.split("=", 1)[0]: load(x.split("=", 1)[1]) for x in sys.argv[1:]}
for name, r in runs.items():
    outs = {v for c in r.values() for v in c}
    print(f"{name}: {len(r)} base layer frames, {len(outs)} distinct composed checksums")
names = list(runs)
for i, a in enumerate(names):
    for b in names[i + 1:]:
        n, same = agree(runs[a], runs[b])
        print(f"{a} vs {b}: {n} shared frames, same composed checksum on {same} ({100 * same / max(n, 1):.1f}%)")
