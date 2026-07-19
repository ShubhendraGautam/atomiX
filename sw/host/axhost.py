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
OP_TPU_GEMM = 0x11
OP_GPU_RUN = 0x12
OP_BYE = 0x7F
ST_OK = 0x00
ROLE_ID_LOOPBACK = 0x4C4F4F50  # "LOOP"
ROLE_ID_TPU = 0x5450554C       # "TPUL"
ROLE_ID_GPU = 0x47505543       # "GPUC"

# gpu-compute ISA (mirror of components/role/gpu-compute/axrole.sv).
GPU_HALT, GPU_TID, GPU_LI, GPU_MOV, GPU_LDX, GPU_STX = 0, 1, 2, 3, 4, 5
GPU_ADD, GPU_SUB, GPU_MUL = 6, 7, 8
GPU_ADDI, GPU_MULI = 17, 18


def gpu_insn(op, rd=0, ra=0, rb=0, imm=0):
    return ((op << 26) | (rd << 23) | (ra << 20) | (rb << 17) |
            (imm & 0x1FFFF)) & 0xFFFFFFFF


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


# Each builder returns (op, request_payload, expected_role_id, verify).  `verify`
# takes the job response payload, raises on mismatch, and returns a summary
# line.  The reference result is computed here on the host, so the role RTL is
# checked against an independent model — not a hand-picked constant.
def build_loopback():
    words = [0x11111111, 0x22222222, 0x33333333, 0xDEADBEEF]

    def verify(resp):
        got = list(struct.unpack(f"<{len(words)}I", resp))
        if got != words:
            raise SystemExit(f"axhost: loopback result {got} != {words}")
        return "loopback copy " + " ".join(f"0x{w:08x}" for w in words)

    return OP_ROLE_RUN, role_run_payload(words), ROLE_ID_LOOPBACK, verify


def build_tpu():
    m = 8
    w = [[((r - c) % 7) - 3 for c in range(8)] for r in range(8)]
    a = [[((i + 2 * k) % 5) - 2 for k in range(8)] for i in range(m)]
    ref = [sum(a[i][k] * w[k][j] for k in range(8))
           for i in range(m) for j in range(8)]
    w_flat = [w[r][c] for r in range(8) for c in range(8)]
    a_flat = [a[i][k] for i in range(m) for k in range(8)]
    payload = bytes([m, 0]) + struct.pack("64b", *w_flat) + \
        struct.pack(f"{8 * m}b", *a_flat)

    def verify(resp):
        got = list(struct.unpack(f"<{m * 8}i", resp))
        if got != ref:
            raise SystemExit(f"axhost: tpu C {got} != reference {ref}")
        return f"int8 GEMM {m}x8x8 -> {m * 8} int32 results verified"

    return OP_TPU_GEMM, payload, ROLE_ID_TPU, verify


def build_gpu():
    n = 10
    base_a, base_b, base_c = 0, 64, 128
    ndata = base_c + n
    a = [i + 1 for i in range(n)]
    b = [100 + 2 * i for i in range(n)]
    data = [0] * ndata
    for i in range(n):
        data[base_a + i] = a[i]
        data[base_b + i] = b[i]
    # SIMT saxpy: C[tid] = 3*A[tid] + B[tid].
    prog = [
        gpu_insn(GPU_TID, rd=0),
        gpu_insn(GPU_LDX, rd=1, ra=0),
        gpu_insn(GPU_ADDI, rd=2, ra=0, imm=base_b),
        gpu_insn(GPU_LDX, rd=3, ra=2),
        gpu_insn(GPU_MULI, rd=1, ra=1, imm=3),
        gpu_insn(GPU_ADD, rd=1, ra=1, rb=3),
        gpu_insn(GPU_ADDI, rd=4, ra=0, imm=base_c),
        gpu_insn(GPU_STX, ra=4, rb=1),
        gpu_insn(GPU_HALT),
    ]
    ref = list(data)
    for i in range(n):
        ref[base_c + i] = (3 * a[i] + b[i]) & 0xFFFFFFFF
    payload = struct.pack("<HHH", n, len(prog), ndata) + \
        b"".join(struct.pack("<I", x) for x in prog) + \
        b"".join(struct.pack("<I", x & 0xFFFFFFFF) for x in data)

    def verify(resp):
        got = list(struct.unpack(f"<{ndata}I", resp))
        if got != ref:
            raise SystemExit(
                f"axhost: gpu C {got[base_c:]} != {ref[base_c:]}")
        return (f"SIMT saxpy over {n} threads -> "
                f"C[{base_c}:{base_c + n}] verified")

    return OP_GPU_RUN, payload, ROLE_ID_GPU, verify


BUILDERS = {"loopback": build_loopback, "tpu": build_tpu, "gpu": build_gpu}


def demo(pipe, role):
    op, job_payload, expect_id, verify = BUILDERS[role]()
    stream = (request(OP_PING) + request(OP_INFO) +
              request(op, job_payload) + request(OP_BYE))
    frames = parse_responses(pipe.exchange(stream))

    # The BYE acknowledgement is optional (the model may halt as it drains), so
    # require the three meaningful responses in order.
    if len(frames) < 3:
        raise SystemExit(f"axhost: expected >=3 responses, got {len(frames)}")

    ping_status, ping_payload = frames[0]
    if ping_status != ST_OK or ping_payload != b"aXHL":
        raise SystemExit(f"axhost: bad PING response {frames[0]!r}")
    print(f"PING -> ok ({ping_payload.decode('ascii', 'replace')})")

    info_status, info_payload = frames[1]
    if info_status != ST_OK or len(info_payload) != 8:
        raise SystemExit(f"axhost: bad INFO response {frames[1]!r}")
    role_id, version = struct.unpack("<II", info_payload)
    if role_id != expect_id:
        raise SystemExit(
            f"axhost: INFO role_id 0x{role_id:08x} != {role} 0x{expect_id:08x}")
    print(f"INFO -> role_id=0x{role_id:08x} ({role}) version={version}")

    job_status, job_resp = frames[2]
    if job_status != ST_OK:
        raise SystemExit(f"axhost: job status {job_status}, frame {frames[2]!r}")
    print(f"JOB  -> {verify(job_resp)}")
    print(f"axhost: host-link demo PASS "
          f"(host drove role.{role} over the link)")


def main():
    parser = argparse.ArgumentParser(description="atomiX host-link driver")
    parser.add_argument("--demo", action="store_true",
                        help="run PING/INFO and a job against the selected role")
    parser.add_argument("--role", choices=sorted(BUILDERS), default="loopback",
                        help="which role the SoC profile selects")
    parser.add_argument("--image", required=True, help="aXos host-link hex image")
    parser.add_argument("--config", required=True, help="SoC profile (selects the role)")
    parser.add_argument("--max-cycles", type=int, default=800000)
    args = parser.parse_args()

    pipe = SimPipe(args.image, args.config, args.max_cycles)
    if args.demo:
        demo(pipe, args.role)
    else:
        parser.error("no action requested (try --demo)")


if __name__ == "__main__":
    main()
