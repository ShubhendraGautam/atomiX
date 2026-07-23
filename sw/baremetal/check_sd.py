#!/usr/bin/env python3
"""Run the SPI SDHC initialization and one-sector-read image on RTL."""
import subprocess
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
EXPECTED = "sd: PASS\n"


def main() -> None:
    image = ROOT / "sw/baremetal/build/sd_test.img"
    subprocess.run(["python3", "make_sd_image.py", str(image)], cwd=ROOT / "sw/baremetal",
                   check=True)
    try:
        result = subprocess.run(
            ["make", "-s", "--no-print-directory", "-C", str(ROOT / "sim/soc"),
             "run", f"RAM_INIT_FILE={ROOT / 'sw/baremetal/build/sd.hex'}",
             "RESET_PC=0x80000000", f"SD_IMAGE={image}", "MAX_CYCLES=100000"],
            cwd=ROOT, text=True, capture_output=True, timeout=60)
    except subprocess.TimeoutExpired:
        raise SystemExit("[baremetal] SD RTL: TIMEOUT")
    if result.returncode:
        sys.stderr.write(result.stdout)
        sys.stderr.write(result.stderr)
        raise SystemExit(f"[baremetal] SD RTL: exit {result.returncode}")
    if result.stdout != EXPECTED:
        sys.stderr.write(result.stderr)
        raise SystemExit(
            f"[baremetal] SD RTL: UART mismatch\n"
            f"  expected: {EXPECTED!r}\n  got:      {result.stdout!r}")
    print("[baremetal] SD RTL: PASS")


if __name__ == "__main__":
    main()
