#!/usr/bin/env python3
"""Build an SDHC image: boot header, raw kernel, then AXFS at block 64."""
import struct
import sys
from pathlib import Path

FILES = {"motd": b"Welcome to aXos.\n",
         "readme": b"aXos SD disk: help, ls, cat, echo, exit.\n"}
FS_BLOCK = 64

def main():
    if len(sys.argv) != 3:
        raise SystemExit("usage: make_boot_disk.py KERNEL.bin OUTPUT.img")
    kernel = Path(sys.argv[1]).read_bytes()
    count = (len(kernel) + 511) // 512
    if count == 0 or count >= FS_BLOCK:
        raise SystemExit("kernel does not fit boot image")
    sectors = [bytearray(512) for _ in range(FS_BLOCK + 3)]
    sectors[0][:8] = b"AXBT" + struct.pack("<I", count)
    for block in range(count):
        sectors[block + 1][:] = kernel[block * 512:(block + 1) * 512].ljust(512, b"\0")
    fs = sectors[FS_BLOCK]
    fs[:6] = b"AXFS\x01\x02"
    for index, (name, data) in enumerate(FILES.items(), 1):
        at = 8 + (index - 1) * 24
        fs[at:at + 16] = name.encode().ljust(16, b"\0")
        fs[at + 16:at + 24] = struct.pack("<II", FS_BLOCK + index, len(data))
        sectors[FS_BLOCK + index][:] = data.ljust(512, b"\0")
    Path(sys.argv[2]).write_bytes(b"".join(sectors))
if __name__ == "__main__": main()
