#!/usr/bin/env python3
"""Create the deterministic one-sector SDHC image used by check-sd."""
import sys
from pathlib import Path


def main() -> None:
    if len(sys.argv) != 2:
        raise SystemExit("usage: make_sd_image.py OUTPUT.img")
    image = bytearray(range(256)) + bytearray(range(256))
    image[:17] = b"atomiX SD block 0"
    Path(sys.argv[1]).write_bytes(image)


if __name__ == "__main__":
    main()
