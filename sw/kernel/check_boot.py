#!/usr/bin/env python3
"""Run the initial S-mode/Sv32 aXos image on ISS, QEMU, and RTL."""
import os
import subprocess
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
EXPECTED = "aXos: S-mode Sv32 timer online\n"


def run(label: str, command: list[str]) -> None:
    try:
        result = subprocess.run(command, cwd=ROOT, text=True,
                                capture_output=True, timeout=15)
    except subprocess.TimeoutExpired:
        if label == "QEMU":
            raise SystemExit(
                "[kernel] QEMU: TIMEOUT (QEMU 6.2 has an upstream RISC-V "
                "PMP bug on mret to S/U; install QEMU >= 7 and use "
                "-cpu rv32,pmp=false; see docs/toolchain.md)")
        raise SystemExit(f"[kernel] {label}: TIMEOUT")
    if result.returncode:
        sys.stderr.write(result.stdout)
        sys.stderr.write(result.stderr)
        raise SystemExit(f"[kernel] {label}: exit {result.returncode}")
    if result.stdout != EXPECTED:
        sys.stderr.write(result.stderr)
        raise SystemExit(
            f"[kernel] {label}: UART mismatch\n"
            f"  expected: {EXPECTED!r}\n  got:      {result.stdout!r}")
    print(f"[kernel] {label}: PASS")


def main() -> None:
    elf = ROOT / "sw/kernel/build/axos_boot.elf"
    image = ROOT / "sw/kernel/build/axos_boot.hex"
    qemu = os.environ.get("QEMU", "qemu-system-riscv32")
    run("ISS", [str(ROOT / "sim/axsim/axsim"), "--bin", str(elf)])
    run("QEMU", [qemu, "-machine", "virt", "-bios", "none",
                 "-cpu", "rv32,pmp=false", "-nographic", "-kernel", str(elf)])
    run("RTL", ["make", "-s", "--no-print-directory", "-C",
                str(ROOT / "sim/soc"), "run", f"RAM_INIT_FILE={image}",
                "RESET_PC=0x80000000"])
    print("[kernel] boot: PASS on ISS, QEMU, and RTL")


if __name__ == "__main__":
    main()
