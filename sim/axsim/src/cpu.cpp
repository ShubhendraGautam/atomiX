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

// Exception causes (privileged spec table 3.6); interrupts join with CLINT.
enum : uint32_t {
  EXC_IADDR_MISALIGNED = 0,
  EXC_IACCESS_FAULT = 1,
  EXC_ILLEGAL = 2,
  EXC_BREAKPOINT = 3,
  EXC_LADDR_MISALIGNED = 4,
  EXC_LACCESS_FAULT = 5,
  EXC_SADDR_MISALIGNED = 6,
  EXC_SACCESS_FAULT = 7,
  EXC_ECALL_M = 11,
};

// Common M-mode trap entry. Synchronous traps name the faulting instruction;
// interrupts name the already-retired instruction's successor in mepc.
Stop Cpu::enter_trap(uint32_t cause, uint32_t tval, uint32_t trap_pc,
                     bool is_interrupt) {
  if (trace)
    fprintf(stderr, "          %s cause=%u tval=%08x pc=%08x\n",
            is_interrupt ? "INTERRUPT" : "TRAP", cause, tval, trap_pc);
  csr.mepc = trap_pc;
  csr.mcause = (is_interrupt ? 0x80000000u : 0) | cause;
  csr.mtval = tval;
  const uint32_t mie = (csr.mstatus >> 3) & 1;   // MPIE <= MIE, MIE <= 0
  csr.mstatus = (csr.mstatus & ~0x88u) | (mie << 7) | 0x1800;  // MPP <= M
  pc = csr.mtvec & ~3u;  // direct mode; vectoring applies to interrupts only
  if (csr.mtvec == 0) {
    fprintf(stderr,
            "[axsim] trap (cause=%u tval=%08x) at pc=%08x with mtvec unset — "
            "halting (retired=%" PRIu64 ")\n",
            cause, tval, trap_pc, ninsn);
    return Stop::Fault;
  }
  return Stop::None;
}

// Take an M-mode synchronous trap at the current (faulting) pc. The caller
// returns immediately, so the instruction writes no register and does not
// retire; the next step() fetches from the handler.
Stop Cpu::trap(uint32_t cause, uint32_t tval) {
  last.trap = true;
  last.cause = cause;
  last.tval = tval;
  return enter_trap(cause, tval, pc, false);
}

Stop Cpu::interrupt(uint32_t cause, uint32_t resume_pc) {
  return enter_trap(cause, 0, resume_pc, true);
}

bool Cpu::csr_read(uint32_t addr, uint32_t& val) {
  switch (addr) {
    case 0x300: val = csr.mstatus; return true;
    case 0x301: val = 0x40001100; return true;  // misa: RV32IM (MXL=1, I/M)
    case 0x304: val = csr.mie; return true;
    case 0x305: val = csr.mtvec; return true;
    case 0x340: val = csr.mscratch; return true;
    case 0x341: val = csr.mepc; return true;
    case 0x342: val = csr.mcause; return true;
    case 0x343: val = csr.mtval; return true;
    case 0x344: val = csr.mip; return true;
    case 0xF11: case 0xF12: case 0xF13: case 0xF14:  // mvendorid..mhartid
      val = 0; return true;
    case 0xB00: case 0xB02: case 0xC00: case 0xC02:  // mcycle/minstret (+u)
      val = (uint32_t)ninsn; return true;
    case 0xB80: case 0xB82: case 0xC80: case 0xC82:  // high halves
      val = (uint32_t)(ninsn >> 32); return true;
    default:
      return false;
  }
}

bool Cpu::csr_write(uint32_t addr, uint32_t val) {
  if (((addr >> 10) & 3) == 3) return false;  // read-only CSR space
  switch (addr) {
    // WARL fields: only M-mode exists, so MPP is pinned to 11; writable
    // mstatus bits are MIE(3) and MPIE(7).
    case 0x300: csr.mstatus = (val & 0x88u) | 0x1800; return true;
    case 0x301: return true;                     // misa: writes ignored
    case 0x304: csr.mie = val; return true;
    case 0x305: csr.mtvec = val & ~2u; return true;  // mode: direct/vectored
    case 0x340: csr.mscratch = val; return true;
    case 0x341: csr.mepc = val & ~3u; return true;   // IALIGN=32
    case 0x342: csr.mcause = val; return true;
    case 0x343: csr.mtval = val; return true;
    case 0x344: return true;  // mip bits are hardware-set; writes ignored
    case 0xB00: case 0xB02: case 0xB80: case 0xB82:
      return true;  // counter writes ignored (served from retired count)
    default:
      return false;
  }
}

Stop Cpu::step() {
  last = {};
  last.valid = true;
  last.pc = pc;
  // Match aXcore's commit-point sampling: this instruction sees the
  // interrupt enables and pending lines from before its own architectural
  // side effects. A CSR write that enables MIE therefore takes effect for the
  // following instruction, not retroactively for itself.
  csr.mip = bus.mip();
  const uint32_t irq_mstatus = csr.mstatus;
  const uint32_t irq_mie = csr.mie;
  const uint32_t irq_mip = csr.mip;
  if (pc & 3) return trap(EXC_IADDR_MISALIGNED, pc);
  uint32_t insn;
  if (!bus.read(pc, 4, insn)) return trap(EXC_IACCESS_FAULT, pc);
  last.insn = insn;

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

  switch (op) {
    case 0x37:  // LUI
      wr = true; wval = imm_u(insn); break;
    case 0x17:  // AUIPC
      wr = true; wval = pc + imm_u(insn); break;
    case 0x6F:  // JAL
      wr = true; wval = pc + 4; next_pc = pc + imm_j(insn); break;
    case 0x67:  // JALR
      if (f3 != 0) return trap(EXC_ILLEGAL, insn);
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
        default: return trap(EXC_ILLEGAL, insn);
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
        default: return trap(EXC_ILLEGAL, insn);
      }
      const uint32_t addr = a + imm_i(insn);
      if (addr % size) return trap(EXC_LADDR_MISALIGNED, addr);
      uint32_t v;
      if (!bus.read(addr, size, v)) return trap(EXC_LACCESS_FAULT, addr);
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
        default: return trap(EXC_ILLEGAL, insn);
      }
      const uint32_t addr = a + imm_s(insn);
      if (addr % size) return trap(EXC_SADDR_MISALIGNED, addr);
      if (!bus.write(addr, size, b)) return trap(EXC_SACCESS_FAULT, addr);
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
          if (f7 != 0) return trap(EXC_ILLEGAL, insn);
          wval = a << (imm & 31);
          break;
        case 5:                                                // SRLI/SRAI
          if (f7 == 0x00) wval = a >> (imm & 31);
          else if (f7 == 0x20) wval = (uint32_t)((int32_t)a >> (imm & 31));
          else return trap(EXC_ILLEGAL, insn);
          break;
      }
      wr = true;
      break;
    }
    case 0x33: {  // OP + RV32M
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
      } else if (f7 == 0x01) {
        const int64_t sa = (int32_t)a, sb = (int32_t)b;
        switch (f3) {
          case 0: wval = (uint32_t)((uint64_t)a * b); break;   // MUL
          case 1: wval = (uint32_t)((uint64_t)(sa * sb) >> 32); break;
          case 2: wval = (uint32_t)((uint64_t)(sa * (int64_t)(uint64_t)b) >> 32); break;
          case 3: wval = (uint32_t)(((uint64_t)a * b) >> 32); break;
          case 4:  // DIV
            wval = b == 0 ? 0xFFFFFFFFu : (uint32_t)(sa / sb); break;
          case 5:  // DIVU
            wval = b == 0 ? 0xFFFFFFFFu : a / b; break;
          case 6:  // REM
            wval = b == 0 ? a : (uint32_t)(sa % sb); break;
          case 7:  // REMU
            wval = b == 0 ? a : a % b; break;
        }
      } else {
        return trap(EXC_ILLEGAL, insn);
      }
      wr = true;
      break;
    }
    case 0x0F:  // MISC-MEM: FENCE/FENCE.I are no-ops — single hart, no
                // caches (DESIGN §4.2); reserved funct3 is illegal (must
                // match the RTL decoder exactly or cosim diverges).
      if (f3 > 1) return trap(EXC_ILLEGAL, insn);
      break;
    case 0x73: {  // SYSTEM
      if (insn == 0x00000073) return trap(EXC_ECALL_M, 0);       // ECALL
      if (insn == 0x00100073) return trap(EXC_BREAKPOINT, pc);   // EBREAK
      if (insn == 0x30200073) {                                  // MRET
        const uint32_t mpie = (csr.mstatus >> 7) & 1;
        csr.mstatus = (csr.mstatus & ~0x8u) | (mpie << 3) | 0x80 | 0x1800;
        next_pc = csr.mepc;
        break;
      }
      if (insn == 0x10500073) break;                             // WFI: nop
      if (f3 == 0 || f3 == 4) return trap(EXC_ILLEGAL, insn);
      // Zicsr: f3 = 1/2/3 register forms, 5/6/7 immediate forms (uimm = rs1
      // field, zero-extended).
      const uint32_t csr_addr = insn >> 20;
      const uint32_t src = (f3 & 4) ? rs1 : a;
      const uint32_t kind = f3 & 3;  // 1=RW 2=RS 3=RC
      uint32_t old;
      if (!csr_read(csr_addr, old)) return trap(EXC_ILLEGAL, insn);
      // RS/RC with rs1=x0 (or uimm=0) are pure reads: no write occurs, so
      // they don't trap on a read-only CSR.
      const bool write = (kind == 1) || rs1 != 0;
      const uint32_t nv =
          kind == 1 ? src : kind == 2 ? (old | src) : (old & ~src);
      if (write && !csr_write(csr_addr, nv)) return trap(EXC_ILLEGAL, insn);
      wr = true;
      wval = old;
      break;
    }
    default:
      return trap(EXC_ILLEGAL, insn);
  }

  if (wr && rd != 0) x[rd] = wval;
  last.rd_we = wr && rd != 0;
  last.rd = rd;
  last.rd_val = wval;

  if (trace) {
    fprintf(stderr, "%8" PRIu64 "  %08x: %08x", ninsn, pc, insn);
    if (wr && rd != 0) fprintf(stderr, "  x%-2u <= %08x", rd, x[rd]);
    fputc('\n', stderr);
  }

  last.retired = true;
  ninsn++;
  pc = next_pc;
  bus.tick();
  csr.mip = bus.mip();
  if ((irq_mstatus & (1u << 3)) && (irq_mie & irq_mip)) {
    const uint32_t pending = irq_mie & irq_mip;
    const uint32_t cause = pending & (1u << 11) ? 11 :
                           pending & (1u << 3) ? 3 : 7;
    return interrupt(cause, next_pc);
  }
  return Stop::None;
}
