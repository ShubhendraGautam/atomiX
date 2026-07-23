#!/usr/bin/env python3
"""Verify FENCE.I updates a cached instruction after a data-store patch."""
import os
import subprocess
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
EXPECTED = "fence.i: PASS\n"


def run(label: str, command: list[str]) -> None:
    try:
        result = subprocess.run(command, cwd=ROOT, text=True,
                                capture_output=True, timeout=60)
    except subprocess.TimeoutExpired:
        raise SystemExit(f"[baremetal] {label}: TIMEOUT")
    if result.returncode:
        sys.stderr.write(result.stdout)
        sys.stderr.write(result.stderr)
        raise SystemExit(f"[baremetal] {label}: exit {result.returncode}")
    if result.stdout != EXPECTED:
        sys.stderr.write(result.stderr)
        raise SystemExit(
            f"[baremetal] {label}: UART mismatch\n"
            f"  expected: {EXPECTED!r}\n  got:      {result.stdout!r}")
    print(f"[baremetal] {label}: PASS")


def main() -> None:
    elf = ROOT / "sw/baremetal/build/fencei.elf"
    image = ROOT / "sw/baremetal/build/fencei.hex"
    qemu = os.environ.get("QEMU", "qemu-system-riscv32")
    run("ISS", [str(ROOT / "sim/axsim/axsim"), "--bin", str(elf),
                "--max", "100000"])
    run("QEMU", [qemu, "-machine", "virt", "-bios", "none",
                 "-nographic", "-kernel", str(elf)])
    run("RTL external memory + caches", [
        "make", "-s", "--no-print-directory", "-C", str(ROOT / "sim/soc"),
        "run", f"RAM_INIT_FILE={image}", "RESET_PC=0x80000000",
        "RAM_BYTES=33554432", "EXTERNAL_MEMORY=1", "CACHES=1",
    ])
    print("[baremetal] FENCE.I: PASS on ISS, QEMU, and cached RTL")


if __name__ == "__main__":
    main()
