#pragma once
#include <cstdint>
#include "bus.h"

// Why execution stopped after a step().
// TODO(phase 0, trap machinery): Ecall/Fault become precise M-mode traps
// (mepc/mcause/mtval + redirect to mtvec) once the CSR file exists; today
// they end the simulation with a diagnostic.
enum class Stop { None, Ebreak, Ecall, Fault };

class Cpu {
 public:
  Cpu(Bus& bus, uint32_t reset_pc) : pc(reset_pc), bus(bus) {}

  Stop step();  // fetch/decode/execute one instruction

  uint32_t pc;
  uint32_t x[32] = {};       // x0 held at zero by the writeback path
  bool trace = false;        // one line per retired instruction, to stderr
  uint64_t retired() const { return ninsn; }

 private:
  Stop fault(const char* what, uint32_t insn);

  Bus& bus;
  uint64_t ninsn = 0;
};
