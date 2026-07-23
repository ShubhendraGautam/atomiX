#!/usr/bin/env python3
"""Run the hello image on ISS, QEMU, and the complete RTL SoC."""
import subprocess
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
HELLO = "hello from atomiX\n"


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
    if result.stdout != HELLO:
        sys.stderr.write(result.stderr)
        raise SystemExit(
            f"[baremetal] {label}: UART mismatch\n"
            f"  expected: {HELLO!r}\n  got:      {result.stdout!r}")
    print(f"[baremetal] {label}: PASS")


def main() -> None:
    elf = ROOT / "sw/baremetal/build/hello.elf"
    image = ROOT / "sw/baremetal/build/hello.hex"
    run("ISS", [str(ROOT / "sim/axsim/axsim"), "--bin", str(elf)])
    run("QEMU", ["qemu-system-riscv32", "-machine", "virt", "-bios", "none",
                 "-nographic", "-kernel", str(elf)])
    run("RTL", ["make", "-s", "--no-print-directory", "-C",
                str(ROOT / "sim/soc"), "run",
                f"RAM_INIT_FILE={image}", "RESET_PC=0x80000000"])
    print("[baremetal] hello: PASS on ISS, QEMU, and RTL")


if __name__ == "__main__":
    main()
