#!/usr/bin/env python3
"""Run aXos shell/fork sessions on all Phase 5 platforms or Phase 6 RTL."""
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
SHELL_OUTPUT_SD = SHELL_OUTPUT.replace(
    "aXos RAM disk: help, ls, cat, echo, exit.\n",
    "aXos SD disk: help, ls, cat, echo, exit.\n")
BOOT_PREFIX = "aXboot\n"
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
    fork_prefix = BOOT_PREFIX + FORK_PREFIX if label.startswith("RTL SD boot") else FORK_PREFIX
    if expected is None and (
            not result.stdout.startswith(fork_prefix) or
            result.stdout[len(fork_prefix):] not in {"PCW", "CPW"}):
        sys.stderr.write(result.stderr)
        raise SystemExit(
            f"[kernel] {label}: fork UART mismatch\n"
            f"  expected: {fork_prefix!r} followed by PCW or CPW\n"
            f"  got:      {result.stdout!r}")
    print(f"[kernel] {label}: PASS")


def main() -> None:
    elf = ROOT / "sw/kernel/build/axos_boot.elf"
    image = ROOT / "sw/kernel/build/axos_boot.hex"
    qemu = os.environ.get("QEMU", "qemu-system-riscv32")
    sd_image = os.environ.get("SD_IMAGE", "")
    if sys.argv[1:] == ["--external-memory"]:
        platforms = [
            ("RTL external memory + caches", [
                "make", "-s", "--no-print-directory", "-C", str(ROOT / "sim/soc"),
                "run", f"RAM_INIT_FILE={image}", "RESET_PC=0x80000000",
                "RAM_BYTES=33554432", "EXTERNAL_MEMORY=1", "CACHES=1",
                "MAX_CYCLES=500000",
            ]),
        ]
        if sd_image:
            platforms[0][1].append(f"SD_IMAGE={sd_image}")
    elif sys.argv[1:] == ["--sd-boot"]:
        boot_rom = os.environ.get("BOOT_ROM", "")
        if not sd_image or not boot_rom:
            raise SystemExit("--sd-boot requires SD_IMAGE and BOOT_ROM")
        platforms = [("RTL SD boot", [
            "make", "-s", "--no-print-directory", "-C", str(ROOT / "sim/soc"),
            "run", f"RAM_INIT_FILE={ROOT / 'sw/bootrom/blank.hex'}",
            f"ROM_INIT_FILE={boot_rom}", "RESET_PC=0x00001000",
            "RAM_BYTES=33554432", "EXTERNAL_MEMORY=1", "CACHES=1",
            "MAX_CYCLES=2000000", "BUILD_ID=sdboot", f"SD_IMAGE={sd_image}",
        ])]
    elif sys.argv[1:]:
        raise SystemExit("usage: check_boot.py [--external-memory|--sd-boot]")
    else:
        platforms = [
        ("ISS", [str(ROOT / "sim/axsim/axsim"), "--bin", str(elf)]),
        ("QEMU", [qemu, "-machine", "virt", "-bios", "none",
                  "-cpu", "rv32,pmp=false", "-nographic", "-kernel", str(elf)]),
        ("RTL", ["make", "-s", "--no-print-directory", "-C",
                 str(ROOT / "sim/soc"), "run", f"RAM_INIT_FILE={image}",
                 "RESET_PC=0x80000000"]),
        ]
    for label, command in platforms:
        shell_command = command + ([f"UART_INPUT_FILE={SHELL_INPUT}"] if label.startswith("RTL")
                                   else ["--uart-input-file", str(SHELL_INPUT)] if label == "ISS"
                                   else [])
        expected_shell = SHELL_OUTPUT_SD if sd_image else SHELL_OUTPUT
        if label == "RTL SD boot": expected_shell = BOOT_PREFIX + expected_shell
        run(f"{label} shell", shell_command, SHELL_INPUT, expected_shell)
        fork_command = command + ([f"UART_INPUT_FILE={FORK_INPUT}"] if label.startswith("RTL")
                                  else ["--uart-input-file", str(FORK_INPUT)] if label == "ISS"
                                  else [])
        run(f"{label} fork", fork_command, FORK_INPUT, None)
    if sys.argv[1:] == ["--external-memory"]:
        print("[kernel] shell + fork/wait: PASS on 32 MiB cached external-memory RTL")
    elif sys.argv[1:] == ["--sd-boot"]:
        print("[kernel] SD boot + AXFS shell: PASS on cached external-memory RTL")
    else:
        print("[kernel] shell + fork/wait: PASS on ISS, QEMU, and RTL")


if __name__ == "__main__":
    main()
