#!/usr/bin/env python3
"""Run the timer-preempted two-task image on ISS, QEMU, and RTL."""
import subprocess
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
EXPECTED = "preempt: ABABAB\n"


def valid_output(label: str, output: str) -> bool:
    if label != "QEMU":
        return output == EXPECTED
    # QEMU's ACLINT advances against host time, so several timer deadlines can
    # expire before a guest task reaches its UART write. The exact string is
    # therefore scheduler-host dependent. Reaching the newline proves all six
    # timer traps ran; seeing both task markers proves a saved frame from each
    # task was restored and executed.
    prefix = "preempt: "
    if not output.startswith(prefix) or not output.endswith("\n"):
        return False
    markers = output[len(prefix):-1]
    return bool(markers) and set(markers) == {"A", "B"}


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
    if not valid_output(label, result.stdout):
        sys.stderr.write(result.stderr)
        expected = (EXPECTED if label != "QEMU"
                    else "'preempt: ' followed by both A and B, then newline")
        raise SystemExit(
            f"[baremetal] {label}: UART mismatch\n"
            f"  expected: {expected!r}\n  got:      {result.stdout!r}")
    print(f"[baremetal] {label}: PASS")


def main() -> None:
    elf = ROOT / "sw/baremetal/build/preempt.elf"
    image = ROOT / "sw/baremetal/build/preempt.hex"
    run("ISS", [str(ROOT / "sim/axsim/axsim"), "--bin", str(elf),
                "--max", "100000"])
    run("QEMU", ["qemu-system-riscv32", "-machine", "virt", "-bios", "none",
                 "-nographic", "-kernel", str(elf)])
    run("RTL", ["make", "-s", "--no-print-directory", "-C",
                str(ROOT / "sim/soc"), "run", f"RAM_INIT_FILE={image}",
                "RESET_PC=0x80000000"])
    print("[baremetal] preempt: PASS on ISS, QEMU, and RTL")


if __name__ == "__main__":
    main()
