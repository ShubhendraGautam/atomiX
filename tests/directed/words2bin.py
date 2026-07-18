#!/usr/bin/env python3
"""words2bin: hex-word listing -> little-endian flat binary.

Input: one 32-bit hex word per line; '#' starts a comment; blank lines ok.
Stopgap for hand-assembled tests until the RISC-V GCC toolchain is installed,
after which directed tests are written as real .S sources.
"""
import struct
import sys

if len(sys.argv) != 3:
    sys.exit(f"usage: {sys.argv[0]} IN.words OUT.bin")

with open(sys.argv[1]) as fin, open(sys.argv[2], "wb") as fout:
    for line in fin:
        tok = line.split("#")[0].strip()
        if tok:
            fout.write(struct.pack("<I", int(tok, 16)))
