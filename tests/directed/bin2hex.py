#!/usr/bin/env python3
"""bin2hex: little-endian flat binary -> one 32-bit $readmemh word per line."""
import argparse
from pathlib import Path


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("input", type=Path)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    data = args.input.read_bytes()
    if len(data) % 4:
        data += b"\0" * (4 - len(data) % 4)
    words = (int.from_bytes(data[i:i + 4], "little")
             for i in range(0, len(data), 4))
    args.output.write_text("".join(f"{word:08x}\n" for word in words))


if __name__ == "__main__":
    main()
