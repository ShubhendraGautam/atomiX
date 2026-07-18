#!/usr/bin/env python3
"""Run the polling SPI smoke image through the complete RTL SoC."""
import subprocess
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
EXPECTED = "spi: PASS\n"


def main() -> None:
    image = ROOT / "sw/baremetal/build/spi.hex"
    try:
        result = subprocess.run(
            ["make", "-s", "--no-print-directory", "-C", str(ROOT / "sim/soc"),
             "run", f"RAM_INIT_FILE={image}", "RESET_PC=0x80000000"],
            cwd=ROOT, text=True, capture_output=True, timeout=15)
    except subprocess.TimeoutExpired:
        raise SystemExit("[baremetal] SPI RTL: TIMEOUT")
    if result.returncode:
        sys.stderr.write(result.stdout)
        sys.stderr.write(result.stderr)
        raise SystemExit(f"[baremetal] SPI RTL: exit {result.returncode}")
    if result.stdout != EXPECTED:
        sys.stderr.write(result.stderr)
        raise SystemExit(
            f"[baremetal] SPI RTL: UART mismatch\n"
            f"  expected: {EXPECTED!r}\n  got:      {result.stdout!r}")
    print("[baremetal] SPI RTL: PASS")


if __name__ == "__main__":
    main()
