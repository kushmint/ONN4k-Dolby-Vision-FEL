#!/usr/bin/env python3
"""Build Module.symvers for dvfel from the CRCs recorded in stock modules.

Kernel symbol CRCs come from the __versions sections of the stock modules and
aml_media's own exports come from its __crc_ symbols. Every value must agree
across all inputs. Usage: gen_symvers.py OUT DVFEL_KO STOCK_KO [STOCK_KO...]
"""
import re
import struct
import subprocess
import sys

out, dvfel, stock = sys.argv[1], sys.argv[2], sys.argv[3:]


def run(*cmd):
    return subprocess.run(cmd, capture_output=True, text=True, check=True).stdout


def versions(path):
    match = re.search(r"\s__versions\s+PROGBITS\s+\S+\s+(\S+)\s+(\S+)", run("llvm-readelf", "-S", "-W", path))
    if not match:
        return {}
    offset, size = int(match[1], 16), int(match[2], 16)
    with open(path, "rb") as handle:
        handle.seek(offset)
        data = handle.read(size)
    return {data[i + 8:i + 64].split(b"\0")[0].decode(): struct.unpack_from("<Q", data, i)[0] & 0xffffffff
            for i in range(0, size, 64)}


kernel, exported, conflicts = {}, {}, set()
for path in stock:
    for name, crc in versions(path).items():
        if kernel.setdefault(name, crc) != crc:
            conflicts.add(name)
    if path.endswith("aml_media.ko"):
        for line in run("llvm-nm", path).splitlines():
            fields = line.split()
            if len(fields) == 3 and fields[2].startswith("__crc_"):
                name, crc = fields[2][6:], int(fields[0], 16) & 0xffffffff
                if exported.setdefault(name, crc) != crc:
                    conflicts.add(name)

needed = sorted({line.split()[-1] for line in run("llvm-nm", "-u", dvfel).splitlines() if line.strip()})
needed = [n for n in needed if n != "__this_module"]
rows, missing = [], []
for name in needed:
    if name in exported:
        rows.append((exported[name], name, "aml_media"))
    elif name in kernel:
        rows.append((kernel[name], name, "vmlinux"))
    else:
        missing.append(name)
with open(out, "w") as handle:
    for crc, name, module in rows:
        handle.write(f"0x{crc:08x}\t{name}\t{module}\tEXPORT_SYMBOL\t\n")
print(f"needed {len(needed)}, resolved {len(rows)}, conflicts among needed "
      f"{sorted(conflicts & set(needed))}")
print("missing:", " ".join(missing) or "none")
