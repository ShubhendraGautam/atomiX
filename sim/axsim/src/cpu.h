#pragma once
#include <cstdint>
#include "bus.h"

// Why step() ended the simulation. Exceptions no longer stop the sim — they
// take a real M-mode trap. Fault remains only for "cannot continue"
// situations (trap taken while mtvec is still 0, i.e. before the program
// installed a handler).
enum class Stop { None, Fault };

// M-mode CSR state. mcycle/minstret are served from the retired-instruction
// counter (cycle == instret in an ISS; the cosim spec will pin down counter
// comparison rules).
struct Csrs {
  uint32_t mstatus = 0x1800;  // MPP=M; MIE clear at reset
  uint32_t mtvec = 0;
  uint32_t mepc = 0;
  uint32_t mcause = 0;
  uint32_t mtval = 0;
  uint32_t mscratch = 0;
  uint32_t mie = 0;
  uint32_t mip = 0;
};

// Architectural result of the most recent step.  This is deliberately a
// machine-readable counterpart to trace mode: cosim consumes it directly
// instead of scraping stderr.  A trap is an executed instruction but not a
// retirement, matching the RTL commit-point convention.
struct StepTrace {
  bool valid = false;
  bool retired = false;
  bool trap = false;
  uint32_t pc = 0;
  uint32_t insn = 0;
  uint32_t cause = 0;
  uint32_t tval = 0;
  bool rd_we = false;
  uint32_t rd = 0;
  uint32_t rd_val = 0;
};

class Cpu {
 public:
  Cpu(Bus& bus, uint32_t reset_pc) : pc(reset_pc), bus(bus) {}

  Stop step();  // fetch/decode/execute one instruction (or take its trap)

  uint32_t pc;
  uint32_t x[32] = {};       // x0 held at zero by the writeback path
  Csrs csr;
  StepTrace last;
  bool trace = false;        // one line per retired instruction, to stderr
  uint64_t retired() const { return ninsn; }

 private:
  Stop trap(uint32_t cause, uint32_t tval);
  bool csr_read(uint32_t addr, uint32_t& val);
  bool csr_write(uint32_t addr, uint32_t val);

  Bus& bus;
  uint64_t ninsn = 0;
};
