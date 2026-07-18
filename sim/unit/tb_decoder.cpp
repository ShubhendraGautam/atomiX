// Unit test for components/core/pipeline5/decoder.sv: expected control bundle per encoding.
#include <cstdio>
#include <cstdint>
#include "Vdecoder.h"
#include "verilated.h"

// Enum encodings mirror axcore_pkg declaration order.
enum { OPA_RS1, OPA_PC, OPA_ZERO };
enum { OPB_RS2, OPB_IMM };
enum { WB_ALU, WB_MEM, WB_PC4, WB_CSR };
enum { MEM_NONE, MEM_LOAD, MEM_STORE };
enum { BR_NONE, BR_COND, BR_JAL, BR_JALR };
enum { CSR_NONE, CSR_RW, CSR_RS, CSR_RC };
enum { SYS_NONE, SYS_ECALL, SYS_EBREAK, SYS_MRET, SYS_SRET, SYS_WFI,
       SYS_FENCE_I, SYS_SFENCE };
enum { IMM_I, IMM_S, IMM_B, IMM_U, IMM_J };

struct Exp {  // defaults mirror the decoder's legal-instruction defaults
  int illegal = 0, rd_we = 0, opa = OPA_RS1, opb = OPB_RS2, imm = IMM_I;
  int alu = 0, wb = WB_ALU, mem = MEM_NONE, br = BR_NONE, csr = CSR_NONE;
  int csr_imm = 0, muldiv = 0, sys = SYS_NONE, rs1 = 0, rs2 = 0;
};

static Vdecoder* dut;
static int failures = 0;

static void chk(const char* name, uint32_t insn, const Exp& e) {
  dut->insn = insn;
  dut->eval();
  auto f = [&](const char* fld, int got, int want) {
    if (got != want) {
      printf("FAIL %s (%08x) %s: got %d want %d\n", name, insn, fld, got, want);
      failures++;
    }
  };
  f("illegal", dut->illegal, e.illegal);
  if (e.illegal) return;  // other outputs are don't-care for illegal insns
  f("rd_we", dut->rd_we, e.rd_we);
  f("opa_sel", dut->opa_sel, e.opa);
  f("opb_sel", dut->opb_sel, e.opb);
  f("imm_sel", dut->imm_sel, e.imm);
  f("alu_op", dut->alu_op, e.alu);
  f("wb_sel", dut->wb_sel, e.wb);
  f("mem_op", dut->mem_op, e.mem);
  f("br_sel", dut->br_sel, e.br);
  f("csr_op", dut->csr_op, e.csr);
  f("csr_imm", dut->csr_imm, e.csr_imm);
  f("muldiv", dut->muldiv, e.muldiv);
  f("sys", dut->sys, e.sys);
  f("uses_rs1", dut->uses_rs1, e.rs1);
  f("uses_rs2", dut->uses_rs2, e.rs2);
}

int main(int argc, char** argv) {
  Verilated::commandArgs(argc, argv);
  dut = new Vdecoder;
  Exp e;

  e = Exp{}; e.rd_we = 1; e.opa = OPA_ZERO; e.opb = OPB_IMM; e.imm = IMM_U;
  chk("lui", 0x10000537, e);

  e = Exp{}; e.rd_we = 1; e.opa = OPA_PC; e.opb = OPB_IMM; e.imm = IMM_U;
  chk("auipc", 0x00001F17, e);

  e = Exp{}; e.rd_we = 1; e.opb = OPB_IMM; e.rs1 = 1;
  chk("addi", 0x00100093, e);

  e = Exp{}; e.rd_we = 1; e.rs1 = 1; e.rs2 = 1; e.alu = 0x8;
  chk("sub", 0x402080B3, e);

  e = Exp{}; e.rd_we = 1; e.opb = OPB_IMM; e.rs1 = 1; e.alu = 0xD;
  chk("srai", 0x4010D093, e);

  e = Exp{}; e.rd_we = 1; e.muldiv = 1; e.rs1 = 1; e.rs2 = 1;
  chk("mul", 0x02208033, e);
  chk("remu", 0x0220F0B3, e);

  e = Exp{}; e.rd_we = 1; e.wb = WB_MEM; e.mem = MEM_LOAD; e.opb = OPB_IMM;
  e.rs1 = 1;
  chk("lw", 0x0000A103, e);

  e = Exp{}; e.mem = MEM_STORE; e.opb = OPB_IMM; e.imm = IMM_S; e.rs1 = 1;
  e.rs2 = 1;
  chk("sb", 0x00B50023, e);

  e = Exp{}; e.br = BR_COND; e.imm = IMM_B; e.rs1 = 1; e.rs2 = 1;
  chk("bne", 0xFE069CE3, e);

  e = Exp{}; e.rd_we = 1; e.wb = WB_PC4; e.br = BR_JAL; e.imm = IMM_J;
  chk("jal", 0x0000006F, e);

  e = Exp{}; e.rd_we = 1; e.wb = WB_PC4; e.br = BR_JALR; e.rs1 = 1;
  chk("jalr", 0x00008067, e);

  e = Exp{}; e.sys = SYS_ECALL;   chk("ecall", 0x00000073, e);
  e = Exp{}; e.sys = SYS_EBREAK;  chk("ebreak", 0x00100073, e);
  e = Exp{}; e.sys = SYS_MRET;    chk("mret", 0x30200073, e);
  e = Exp{}; e.sys = SYS_SRET;    chk("sret", 0x10200073, e);
  e = Exp{}; e.sys = SYS_WFI;     chk("wfi", 0x10500073, e);
  e = Exp{}; e.sys = SYS_FENCE_I; chk("fence.i", 0x0000100F, e);
  // sfence.vma x1, x2 — privilege legality is dynamic, decode is legal
  e = Exp{}; e.sys = SYS_SFENCE;  chk("sfence.vma", 0x12208073, e);
  e = Exp{};                      chk("fence", 0x0000000F, e);

  e = Exp{}; e.rd_we = 1; e.wb = WB_CSR; e.csr = CSR_RW; e.rs1 = 1;
  chk("csrw mtvec", 0x30529073, e);

  // csrr = csrrs rd, csr, x0: register form still reads the rs1 field
  // (x0 hazards are masked later, in the hazard unit — not here).
  e = Exp{}; e.rd_we = 1; e.wb = WB_CSR; e.csr = CSR_RS; e.rs1 = 1;
  chk("csrr mcause", 0x342023F3, e);

  e = Exp{}; e.rd_we = 1; e.wb = WB_CSR; e.csr = CSR_RW; e.csr_imm = 1;
  chk("csrwi mie", 0x3042D073, e);

  // illegal encodings
  e = Exp{}; e.illegal = 1;
  chk("all-zeros", 0x00000000, e);
  chk("all-ones", 0xFFFFFFFF, e);
  chk("slli bad f7", 0x40109093, e);
  chk("OP bad funct7", 0x04208033, e);
  chk("fence f3=2", 0x0000200F, e);
  chk("sfence.vma rd!=0", 0x122080F3, e);
  chk("branch f3=2", 0x00062063, e);
  chk("load f3=3", 0x0000B103, e);

  delete dut;
  if (failures) { printf("tb_decoder: %d FAILURES\n", failures); return 1; }
  printf("tb_decoder: PASS\n");
  return 0;
}
