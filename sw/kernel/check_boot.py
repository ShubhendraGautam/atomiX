#!/usr/bin/env python3
"""Run aXos shell and fork/wait sessions on ISS, QEMU, and RTL."""
import os
import subprocess
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
SHELL_INPUT = ROOT / "sw/kernel/shell_input.txt"
FORK_INPUT = ROOT / "sw/kernel/fork_input.txt"
SHELL_OUTPUT = (
    "aXos: shell online\n"
    "aXos> help\n"
    "commands: help ls cat echo fork exit\n"
    "aXos> ls\n"
    "motd\n"
    "readme\n"
    "aXos> cat motd\n"
    "Welcome to aXos.\n"
    "aXos> echo atomiX\n"
    "atomiX\n"
    "aXos> exit\n"
)
FORK_PREFIX = "aXos: shell online\naXos> fork\nfork demo: "


def run(label: str, command: list[str], input_file: Path, expected: str | None) -> None:
    try:
        result = subprocess.run(command, cwd=ROOT, text=True,
                                input=input_file.read_text(), capture_output=True,
                                timeout=15)
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
    if expected is not None and result.stdout != expected:
        sys.stderr.write(result.stderr)
        raise SystemExit(
            f"[kernel] {label}: UART mismatch\n"
            f"  expected: {expected!r}\n"
            f"  got:      {result.stdout!r}")
    if expected is None and (
            not result.stdout.startswith(FORK_PREFIX) or
            result.stdout[len(FORK_PREFIX):] not in {"PCW", "CPW"}):
        sys.stderr.write(result.stderr)
        raise SystemExit(
            f"[kernel] {label}: fork UART mismatch\n"
            f"  expected: {FORK_PREFIX!r} followed by PCW or CPW\n"
            f"  got:      {result.stdout!r}")
    print(f"[kernel] {label}: PASS")


def main() -> None:
    elf = ROOT / "sw/kernel/build/axos_boot.elf"
    image = ROOT / "sw/kernel/build/axos_boot.hex"
    qemu = os.environ.get("QEMU", "qemu-system-riscv32")
    platforms = [
        ("ISS", [str(ROOT / "sim/axsim/axsim"), "--bin", str(elf)]),
        ("QEMU", [qemu, "-machine", "virt", "-bios", "none",
                  "-cpu", "rv32,pmp=false", "-nographic", "-kernel", str(elf)]),
        ("RTL", ["make", "-s", "--no-print-directory", "-C",
                 str(ROOT / "sim/soc"), "run", f"RAM_INIT_FILE={image}",
                 "RESET_PC=0x80000000"]),
    ]
    for label, command in platforms:
        shell_command = command + ([f"UART_INPUT_FILE={SHELL_INPUT}"] if label == "RTL"
                                   else ["--uart-input-file", str(SHELL_INPUT)] if label == "ISS"
                                   else [])
        run(f"{label} shell", shell_command, SHELL_INPUT, SHELL_OUTPUT)
        fork_command = command + ([f"UART_INPUT_FILE={FORK_INPUT}"] if label == "RTL"
                                  else ["--uart-input-file", str(FORK_INPUT)] if label == "ISS"
                                  else [])
        run(f"{label} fork", fork_command, FORK_INPUT, None)
    print("[kernel] shell + fork/wait: PASS on ISS, QEMU, and RTL")


if __name__ == "__main__":
    main()
