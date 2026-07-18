#!/usr/bin/env python3
"""Generate an isolated riscv-formal worktree below formal/build/."""
from pathlib import Path
import shutil
import subprocess


ROOT = Path(__file__).resolve().parent
BUILD = ROOT / "build" / "riscv-formal"
CORE = BUILD / "cores" / "axcore"
REFERENCE = Path("/opt/riscv-formal")


def link(source: Path, destination: Path) -> None:
    if destination.is_symlink() or destination.exists():
        if destination.is_dir() and not destination.is_symlink():
            shutil.rmtree(destination)
        else:
            destination.unlink()
    destination.symlink_to(source)


def main() -> None:
    if not REFERENCE.is_dir():
        raise SystemExit("missing /opt/riscv-formal; see docs/toolchain.md")

    CORE.mkdir(parents=True, exist_ok=True)
    link(REFERENCE / "checks", BUILD / "checks")
    link(REFERENCE / "insns", BUILD / "insns")
    link(ROOT.parent / "rtl", BUILD / "rtl")
    link(ROOT / "checks.cfg", CORE / "checks.cfg")
    link(ROOT / "axcore_rvfi_wrapper.sv", CORE / "axcore_rvfi_wrapper.sv")
    subprocess.run(
        ["python3", str(REFERENCE / "checks" / "genchecks.py")],
        cwd=CORE,
        check=True,
    )
    # riscv-formal's generator defaults to Yosys's legacy SystemVerilog
    # frontend. aXcore uses a standard package/import for shared types, so
    # use the Slang frontend bundled with current upstream Yosys instead.
    for job in (CORE / "checks").glob("*.sby"):
        contents = job.read_text()
        contents = contents.replace(
            "read -sv ", "read_slang --std 1800-2017 "
        )
        job.write_text(contents)


if __name__ == "__main__":
    main()
