#!/usr/bin/env python3
"""Build a deterministic, writable AXFS v1 image used by RTL storage tests."""
import struct
import sys
from pathlib import Path


FILES = {
    "motd": b"Welcome to aXos.\n",
    "readme": b"aXos SD disk: help, ls, cat, echo, exit.\n",
}


def main() -> None:
    if len(sys.argv) != 2:
        raise SystemExit("usage: make_fs_image.py OUTPUT.img")
    # Keep spare sectors after the directory so a write regression can create
    # files without relying on host-side image growth semantics.
    sectors = [bytearray(512) for _ in range(16)]
    sectors[0][:6] = b"AXFS\x01\x02"
    for index, (name, content) in enumerate(FILES.items(), start=1):
        entry = 8 + (index - 1) * 24
        sectors[0][entry:entry + 16] = name.encode().ljust(16, b"\0")
        sectors[0][entry + 16:entry + 24] = struct.pack("<II", index, len(content))
        sectors[index][:] = content.ljust(512, b"\0")
    Path(sys.argv[1]).write_bytes(b"".join(sectors))


if __name__ == "__main__":
    main()
