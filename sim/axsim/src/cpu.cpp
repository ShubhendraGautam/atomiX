#include "cpu.h"

#include <cstdio>
#include <inttypes.h>

// Immediate extractors, RISC-V unprivileged spec §2.3 (all sign-extended
// except U, which is the raw upper 20 bits).
static uint32_t imm_i(uint32_t i) { return (uint32_t)((int32_t)i >> 20); }
static uint32_t imm_s(uint32_t i) {
  return (uint32_t)((int32_t)(i & 0xFE000000) >> 20) | ((i >> 7) & 0x1F);
}
static uint32_t imm_b(uint32_t i) {
  return (uint32_t)((int32_t)(i & 0x80000000) >> 19) | ((i & 0x80) << 4) |
         ((i >> 20) & 0x7E0) | ((i >> 7) & 0x1E);
}
static uint32_t imm_u(uint32_t i) { return i & 0xFFFFF000; }
static uint32_t imm_j(uint32_t i) {
  return (uint32_t)((int32_t)(i & 0x80000000) >> 11) | (i & 0xFF000) |
         ((i >> 9) & 0x800) | ((i >> 20) & 0x7FE);
}

Stop Cpu::fault(const char* what, uint32_t insn) {
  fprintf(stderr, "[axsim] FAULT: %s  pc=%08x insn=%08x (retired=%" PRIu64 ")\n",
          what, pc, insn, ninsn);
  return Stop::Fault;
}

Stop Cpu::step() {
  if (pc & 3) return fault("misaligned pc", 0);
  uint32_t insn;
  if (!bus.read(pc, 4, insn)) return fault("instruction access fault", 0);

  const uint32_t op = insn & 0x7F;
  const uint32_t rd = (insn >> 7) & 0x1F;
  const uint32_t f3 = (insn >> 12) & 0x7;
  const uint32_t rs1 = (insn >> 15) & 0x1F;
  const uint32_t rs2 = (insn >> 20) & 0x1F;
  const uint32_t f7 = insn >> 25;
  const uint32_t a = x[rs1];
  const uint32_t b = x[rs2];

  uint32_t next_pc = pc + 4;
  bool wr = false;
  uint32_t wval = 0;
  Stop stop = Stop::None;

  switch (op) {
    case 0x37:  // LUI
      wr = true; wval = imm_u(insn); break;
    case 0x17:  // AUIPC
      wr = true; wval = pc + imm_u(insn); break;
    case 0x6F:  // JAL
      wr = true; wval = pc + 4; next_pc = pc + imm_j(insn); break;
    case 0x67:  // JALR
      if (f3 != 0) return fault("illegal instruction", insn);
      wr = true; wval = pc + 4;
      next_pc = (a + imm_i(insn)) & ~1u;
      break;
    case 0x63: {  // branches
      bool taken;
      switch (f3) {
        case 0: taken = a == b; break;                        // BEQ
        case 1: taken = a != b; break;                        // BNE
        case 4: taken = (int32_t)a < (int32_t)b; break;       // BLT
        case 5: taken = (int32_t)a >= (int32_t)b; break;      // BGE
        case 6: taken = a < b; break;                         // BLTU
        case 7: taken = a >= b; break;                        // BGEU
        default: return fault("illegal instruction", insn);
      }
      if (taken) next_pc = pc + imm_b(insn);
      break;
    }
    case 0x03: {  // loads
      unsigned size;
      switch (f3) {
        case 0: case 4: size = 1; break;  // LB, LBU
        case 1: case 5: size = 2; break;  // LH, LHU
        case 2: size = 4; break;          // LW
        default: return fault("illegal instruction", insn);
      }
      const uint32_t addr = a + imm_i(insn);
      if (addr % size) return fault("misaligned load", insn);
      uint32_t v;
      if (!bus.read(addr, size, v)) return fault("load access fault", insn);
      if (f3 == 0) v = (uint32_t)(int32_t)(int8_t)v;
      if (f3 == 1) v = (uint32_t)(int32_t)(int16_t)v;
      wr = true; wval = v;
      break;
    }
    case 0x23: {  // stores
      unsigned size;
      switch (f3) {
        case 0: size = 1; break;  // SB
        case 1: size = 2; break;  // SH
        case 2: size = 4; break;  // SW
        default: return fault("illegal instruction", insn);
      }
      const uint32_t addr = a + imm_s(insn);
      if (addr % size) return fault("misaligned store", insn);
      if (!bus.write(addr, size, b)) return fault("store access fault", insn);
      break;
    }
    case 0x13: {  // OP-IMM
      const uint32_t imm = imm_i(insn);
      switch (f3) {
        case 0: wval = a + imm; break;                         // ADDI
        case 2: wval = (int32_t)a < (int32_t)imm; break;       // SLTI
        case 3: wval = a < imm; break;                         // SLTIU
        case 4: wval = a ^ imm; break;                         // XORI
        case 6: wval = a | imm; break;                         // ORI
        case 7: wval = a & imm; break;                         // ANDI
        case 1:                                                // SLLI
          if (f7 != 0) return fault("illegal instruction", insn);
          wval = a << (imm & 31);
          break;
        case 5:                                                // SRLI/SRAI
          if (f7 == 0x00) wval = a >> (imm & 31);
          else if (f7 == 0x20) wval = (uint32_t)((int32_t)a >> (imm & 31));
          else return fault("illegal instruction", insn);
          break;
      }
      wr = true;
      break;
    }
    case 0x33: {  // OP (f7=0x01 = M extension, phase 2)
      if (f7 == 0x00) {
        switch (f3) {
          case 0: wval = a + b; break;                         // ADD
          case 1: wval = a << (b & 31); break;                 // SLL
          case 2: wval = (int32_t)a < (int32_t)b; break;       // SLT
          case 3: wval = a < b; break;                         // SLTU
          case 4: wval = a ^ b; break;                         // XOR
          case 5: wval = a >> (b & 31); break;                 // SRL
          case 6: wval = a | b; break;                         // OR
          case 7: wval = a & b; break;                         // AND
        }
      } else if (f7 == 0x20 && f3 == 0) {
        wval = a - b;                                          // SUB
      } else if (f7 == 0x20 && f3 == 5) {
        wval = (uint32_t)((int32_t)a >> (b & 31));             // SRA
      } else {
        return fault("illegal instruction", insn);
      }
      wr = true;
      break;
    }
    case 0x0F:  // FENCE/FENCE.I: no-ops — single hart, no caches (DESIGN §4.2)
      break;
    case 0x73:  // SYSTEM
      if (insn == 0x00000073) stop = Stop::Ecall;
      else if (insn == 0x00100073) stop = Stop::Ebreak;
      else return fault("illegal instruction (Zicsr lands with the CSR file)",
                        insn);
      break;
    default:
      return fault("illegal instruction", insn);
  }

  if (wr && rd != 0) x[rd] = wval;

  if (trace) {
    fprintf(stderr, "%8" PRIu64 "  %08x: %08x", ninsn, pc, insn);
    if (wr && rd != 0) fprintf(stderr, "  x%-2u <= %08x", rd, x[rd]);
    fputc('\n', stderr);
  }

  ninsn++;
  pc = next_pc;
  return stop;
}
