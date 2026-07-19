#!/usr/bin/env python3
"""axhost — host-side driver for the atomiX shell + role platform.

Speaks the aX host-link protocol (docs/host-protocol.md) to the aXos host-link
service running on the FPGA shell.  This is the base: it uses the virtual-pipe
backend, talking to a Verilator simulation of the shell over the console byte
pipe — request bytes go in through the harness UART input, response bytes come
back on the model's stdout.  The USB-serial backend for real hardware reuses the
same frame codec below; only `SimPipe` is replaced.

    axhost.py --demo --image <axos.hex> --config <profile.json>

`--demo` runs PING, INFO, and a ROLE_RUN job against role.loopback and checks
every response, which is what `make -C sw/kernel check-hostlink` invokes.
"""
import argparse
import struct
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]

REQ_SYNC = 0xA5
RSP_SYNC = 0x5A
OP_PING = 0x01
OP_INFO = 0x02
OP_ROLE_RUN = 0x10
OP_BYE = 0x7F
ST_OK = 0x00
ROLE_ID_LOOPBACK = 0x4C4F4F50  # "LOOP"


def request(op, payload=b""):
    return bytes([REQ_SYNC, op]) + struct.pack("<H", len(payload)) + payload


def role_run_payload(words):
    return struct.pack("<H", len(words)) + b"".join(
        struct.pack("<I", w & 0xFFFFFFFF) for w in words)


def parse_responses(data):
    """Return a list of (status, payload) frames, skipping any non-frame
    preamble and resynchronizing on the response sync byte."""
    frames = []
    i = 0
    while i + 4 <= len(data):
        if data[i] != RSP_SYNC:
            i += 1
            continue
        status = data[i + 1]
        length = data[i + 2] | (data[i + 3] << 8)
        if i + 4 + length > len(data):
            break
        frames.append((status, data[i + 4:i + 4 + length]))
        i += 4 + length
    return frames


class SimPipe:
    """Virtual-pipe transport: drive a Verilator simulation of the shell."""

    def __init__(self, image, config, max_cycles):
        self.image = image
        self.config = config
        self.max_cycles = max_cycles

    def exchange(self, request_bytes):
        with tempfile.NamedTemporaryFile(suffix=".req", delete=False) as handle:
            handle.write(request_bytes)
            req_path = handle.name
        command = [
            "make", "-s", "--no-print-directory", "-C", str(ROOT / "sim/soc"),
            "run", f"RAM_INIT_FILE={self.image}", "RESET_PC=0x80000000",
            f"COMPONENT_CONFIG={self.config}", f"MAX_CYCLES={self.max_cycles}",
            f"UART_INPUT_FILE={req_path}", "BUILD_ID=hostlink",
        ]
        result = subprocess.run(command, cwd=ROOT, capture_output=True,
                                timeout=240)
        if result.returncode:
            sys.stderr.write(result.stderr.decode("latin-1", "replace"))
            raise SystemExit(f"axhost: simulation exit {result.returncode}")
        return result.stdout


def demo(pipe):
    words = [0x11111111, 0x22222222, 0x33333333, 0xDEADBEEF]
    stream = (request(OP_PING) + request(OP_INFO) +
              request(OP_ROLE_RUN, role_run_payload(words)) + request(OP_BYE))
    frames = parse_responses(pipe.exchange(stream))

    # The BYE acknowledgement is optional (the model may halt as it drains), so
    # require the three meaningful responses in order.
    if len(frames) < 3:
        raise SystemExit(f"axhost: expected >=3 responses, got {len(frames)}")

    ping_status, ping_payload = frames[0]
    if ping_status != ST_OK or ping_payload != b"aXHL":
        raise SystemExit(f"axhost: bad PING response {frames[0]!r}")
    print(f"PING     -> ok ({ping_payload.decode('ascii', 'replace')})")

    info_status, info_payload = frames[1]
    if info_status != ST_OK or len(info_payload) != 8:
        raise SystemExit(f"axhost: bad INFO response {frames[1]!r}")
    role_id, version = struct.unpack("<II", info_payload)
    if role_id != ROLE_ID_LOOPBACK:
        raise SystemExit(f"axhost: INFO role_id 0x{role_id:08x} != loopback")
    print(f"INFO     -> role_id=0x{role_id:08x} (loopback) version={version}")

    run_status, run_payload = frames[2]
    if run_status != ST_OK or len(run_payload) != 4 * len(words):
        raise SystemExit(f"axhost: bad ROLE_RUN response {frames[2]!r}")
    got = list(struct.unpack(f"<{len(words)}I", run_payload))
    if got != words:
        raise SystemExit(f"axhost: ROLE_RUN result {got} != input {words}")
    print("ROLE_RUN -> " + " ".join(f"0x{w:08x}" for w in got) + " (verified)")
    print("axhost: host-link demo PASS "
          "(host discovered and drove role.loopback over the link)")


def main():
    parser = argparse.ArgumentParser(description="atomiX host-link driver")
    parser.add_argument("--demo", action="store_true",
                        help="run PING/INFO/ROLE_RUN against role.loopback")
    parser.add_argument("--image", required=True, help="aXos host-link hex image")
    parser.add_argument("--config", required=True, help="SoC profile (selects the role)")
    parser.add_argument("--max-cycles", type=int, default=400000)
    args = parser.parse_args()

    pipe = SimPipe(args.image, args.config, args.max_cycles)
    if args.demo:
        demo(pipe)
    else:
        parser.error("no action requested (try --demo)")


if __name__ == "__main__":
    main()
