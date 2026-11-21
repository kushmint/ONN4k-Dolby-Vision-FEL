#!/usr/bin/env python3
"""Read export CRCs from an uncompressed arm64 kernel Image.

Each export table is located from its PREL32 name pointers, then its CRC table
is located and validated against CRCs already known from stock modules. A table
is used only if every known symbol in it agrees.
Usage: image_crcs.py IMAGE KNOWN_SYMVERS SYMBOL [SYMBOL...]
"""
import struct
import sys

image_path, known_path, wanted = sys.argv[1], sys.argv[2], sys.argv[3:]
image = open(image_path, "rb").read()
known = {}
for line in open(known_path):
    fields = line.split("\t")
    if fields[2] == "vmlinux":
        known[fields[1]] = int(fields[0], 16)


def s32(pos):
    return struct.unpack_from("<i", image, pos)[0]


def name_at(pos):
    if not 0 <= pos < len(image):
        return None
    end = image.find(b"\0", pos, pos + 128)
    if end <= pos:
        return None
    raw = image[pos:end]
    if not all(c == 95 or 48 <= c <= 57 or 65 <= c <= 90 or 97 <= c <= 122 or c == 46 for c in raw):
        return None
    return raw.decode()


def entry_name(pos):
    return name_at(pos + 4 + s32(pos + 4)) if 0 <= pos <= len(image) - 12 else None


anchors = sorted(known)[:60] + wanted
tables = {}
for symbol in anchors:
    needle = b"\0" + symbol.encode() + b"\0"
    start = image.find(needle)
    while start >= 0:
        target = start + 1
        # Export entries live within 64 MiB of their strings.
        low, high = max(0, target - 0x4000000) & ~3, min(len(image) - 12, target + 0x4000000) & ~3
        for pos in range(low, high, 4):
            if pos + 4 + s32(pos + 4) != target:
                continue
            first = pos
            while entry_name(first - 12):
                first -= 12
            last = pos
            while entry_name(last + 12):
                last += 12
            if last - first >= 12 * 32:
                tables[first] = last
        start = image.find(needle, start + 1)
    if len(tables) >= 2 and all(any(entry_name(p) == w for f, l in tables.items()
                                    for p in range(f, l + 12, 12)) for w in wanted):
        break

found = {}
for first, last in sorted(tables.items()):
    names = [entry_name(p) for p in range(first, last + 12, 12)]
    index = {n: i for i, n in enumerate(names)}
    checks = [(index[n], known[n]) for n in names if n in known]
    print(f"table at {first:#x}: {len(names)} exports, {len(checks)} with known CRCs", file=sys.stderr)
    if len(checks) < 8:
        continue
    i0, c0 = checks[0]
    needle = struct.pack("<I", c0)
    pos = image.find(needle)
    match = None
    while pos >= 0:
        base = pos - i0 * 4
        if pos % 4 == 0 and base >= 0 and base + len(names) * 4 <= len(image) and all(
                struct.unpack_from("<I", image, base + i * 4)[0] == c for i, c in checks):
            match = base
            break
        pos = image.find(needle, pos + 1)
    print(f"  CRC table: {hex(match) if match is not None else 'not validated'}", file=sys.stderr)
    if match is None:
        continue
    for symbol in wanted:
        if symbol in index:
            found[symbol] = struct.unpack_from("<I", image, match + index[symbol] * 4)[0]

for symbol in wanted:
    if symbol in found:
        print(f"0x{found[symbol]:08x}\t{symbol}\tvmlinux\tEXPORT_SYMBOL\t")
    else:
        print(f"UNRESOLVED {symbol}", file=sys.stderr)
sys.exit(0 if len(found) == len(wanted) else 1)
