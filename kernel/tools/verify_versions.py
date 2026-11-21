#!/usr/bin/env python3
"""Confirm every symbol version recorded in a built module matches the expected CRC.
Usage: verify_versions.py MODULE_KO SYMVERS"""
import re
import struct
import subprocess
import sys

ko, symvers = sys.argv[1:3]
want = {line.split("\t")[1]: int(line.split("\t")[0], 16) for line in open(symvers)}
sections = subprocess.run(["llvm-readelf", "-S", "-W", ko], capture_output=True, text=True).stdout
match = re.search(r"\s__versions\s+PROGBITS\s+\S+\s+(\S+)\s+(\S+)", sections)
offset, size = int(match[1], 16), int(match[2], 16)
data = open(ko, "rb").read()[offset:offset + size]
got = {data[i + 8:i + 64].split(b"\0")[0].decode(): struct.unpack_from("<Q", data, i)[0]
       for i in range(0, size, 64)}
bad = [name for name, crc in got.items() if crc == 0 or want.get(name) != crc]
print(f"__versions entries: {len(got)}, mismatched: {bad or 'none'}, "
      f"module_layout: {got.get('module_layout', 0):#010x}")
sys.exit(1 if bad else 0)
