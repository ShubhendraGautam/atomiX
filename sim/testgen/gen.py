#!/usr/bin/env python3
"""Generate a deterministic, self-terminating RV32I+Zicsr fuzz program.

The stream stays in a single linear basic block so every generated instruction
executes exactly once.  Taken branches and JAL still redirect to PC+4, which
exercises the pipeline flush machinery without making test length data
dependent.  Memory accesses use x1, held at the RAM base, and all generated
addresses are aligned and in range.
"""
import argparse
import random
import struct


def r_type(f7, rs2, rs1, f3, rd):
    return (f7 << 25) | (rs2 << 20) | (rs1 << 15) | (f3 << 12) | (rd << 7) | 0x33


def i_type(imm, rs1, f3, rd, opcode=0x13):
    return ((imm & 0xfff) << 20) | (rs1 << 15) | (f3 << 12) | (rd << 7) | opcode


def s_type(imm, rs2, rs1, f3):
    imm &= 0xfff
    return ((imm >> 5) << 25) | (rs2 << 20) | (rs1 << 15) | (f3 << 12) | \
           ((imm & 0x1f) << 7) | 0x23


def b_type(imm, rs2, rs1, f3):
    imm &= 0x1fff
    return ((imm >> 12) << 31) | (((imm >> 5) & 0x3f) << 25) | \
           (rs2 << 20) | (rs1 << 15) | (f3 << 12) | \
           (((imm >> 1) & 0xf) << 8) | (((imm >> 11) & 1) << 7) | 0x63


def j_type(imm, rd):
    imm &= 0x1fffff
    return ((imm >> 20) << 31) | (((imm >> 1) & 0x3ff) << 21) | \
           (((imm >> 11) & 1) << 20) | (((imm >> 12) & 0xff) << 12) | \
           (rd << 7) | 0x6f


def csr_type(csr, rs1, f3, rd):
    return (csr << 20) | (rs1 << 15) | (f3 << 12) | (rd << 7) | 0x73


def reg(rng):
    # x1 stays the RAM base and x0 remains architectural zero.
    return rng.randrange(2, 32)


def instruction(rng):
    kind = rng.randrange(12)
    rd, rs1, rs2 = reg(rng), reg(rng), reg(rng)
    if kind == 0:  # RV32I register ALU (ADD/SUB/SLL/SLT/SLTU/XOR/SRL/SRA/OR/AND)
        f3 = rng.randrange(8)
        f7 = 0x20 if f3 in (0, 5) and rng.randrange(2) else 0
        return r_type(f7, rs2, rs1, f3, rd)
    if kind == 1:  # RV32I immediate ALU
        f3 = rng.choice((0, 2, 3, 4, 6, 7))
        return i_type(rng.randrange(-2048, 2048), rs1, f3, rd)
    if kind == 2:  # shifts have constrained funct7/shamt encodings
        shamt = rng.randrange(32)
        if rng.randrange(2):
            return i_type(shamt, rs1, 1, rd)
        return i_type(shamt | (0x400 if rng.randrange(2) else 0), rs1, 5, rd)
    if kind == 3:  # load from the protected RAM-base register
        f3 = rng.choice((0, 1, 2, 4, 5))
        align = 4 if f3 == 2 else 2 if f3 in (1, 5) else 1
        return i_type(rng.randrange(0, 1024 // align) * align, 1, f3, rd, 0x03)
    if kind == 4:  # store to protected RAM-base register
        f3 = rng.choice((0, 1, 2))
        align = 4 if f3 == 2 else 2 if f3 == 1 else 1
        return s_type(rng.randrange(0, 1024 // align) * align, rs2, 1, f3)
    if kind == 5:
        return ((rng.randrange(1 << 20)) << 12) | (rd << 7) | 0x37  # LUI
    if kind == 6:
        return ((rng.randrange(1 << 20)) << 12) | (rd << 7) | 0x17  # AUIPC
    if kind == 7:  # redirect, but target is the ordinary next instruction
        return b_type(4, 0, 0, rng.choice((0, 1, 4, 5, 6, 7)))
    if kind == 8:
        return j_type(4, rd)  # JAL with an architecturally visible link value
    if kind == 9:  # harmless serializing cache barrier forms
        return rng.choice((0x0000000f, 0x0000100f))
    if kind == 10:
        # Keep CSR traffic away from timing counters: architectural cycle
        # counts differ between an ISS and a wait-state-tolerant pipeline.
        csr = rng.choice((0x300, 0x304, 0x305, 0x340, 0x341, 0x342, 0x343, 0x344))
        return csr_type(csr, rs1 if rng.randrange(2) else 0,
                        rng.choice((1, 2, 3, 5, 6, 7)), rd)
    # RV32M: the serializing 32-cycle execution path and its forwarding
    # boundary are a principal Phase-2 fuzz target.
    return r_type(1, rs2, rs1, rng.randrange(8), rd)


def program(seed, count):
    rng = random.Random(seed)
    # x1 = 0x8100_0000, a 16 MiB RAM data window safely beyond the default
    # 1,000,000-instruction (4 MiB) image; x2 starts known-zero for early
    # dependencies.
    words = [0x810000b7, 0x00000113]
    words.extend(instruction(rng) for _ in range(count))
    # QEMU sifive_test-compatible clean exit: sw 0x5555, 0x0010_0000(x5).
    words.extend((0x001002b7, 0x00005337, 0x55530313, 0x0062a023))
    return words


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--seed', type=lambda n: int(n, 0), required=True)
    parser.add_argument('--count', type=int, required=True)
    parser.add_argument('--out', required=True)
    args = parser.parse_args()
    if args.count < 1:
        parser.error('--count must be positive')
    with open(args.out, 'wb') as out:
        for word in program(args.seed, args.count):
            out.write(struct.pack('<I', word))


if __name__ == '__main__':
    main()
