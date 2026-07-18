// Lock-step cosimulation: drive aXcore through a behavioral bus, execute the
// same architectural event in aXsim, then compare the complete event record
// and M-mode CSR state.  RTL and ISS own separate RAM images so a matching
// result cannot be caused by shared mutable state.
//
// usage: axcosim --bin PROGRAM [--ws N] [--max CYCLES] [--trace]
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdint>
#include <vector>

#include "Vaxcore.h"
#include "verilated.h"
#include "bus.h"
#include "cpu.h"
#include "elf.h"

static constexpr uint32_t RAM_BASE = map::RAM_BASE;
static constexpr uint32_t RAM_SIZE = 32u << 20;
static constexpr uint32_t UART_BASE = map::UART_BASE;
static constexpr uint32_t TEST_BASE = map::TEST_BASE;

struct RtlBus {
  std::vector<uint8_t> ram = std::vector<uint8_t>(RAM_SIZE, 0);
  bool tohost_en = false;
  uint32_t tohost_addr = 0;
  bool exit_req = false;
  int exit_code = 0;

  bool is_ram(uint32_t addr) const {
    return addr >= RAM_BASE && addr - RAM_BASE <= RAM_SIZE - 4;
  }
  bool mapped(uint32_t addr) const {
    return is_ram(addr) ||
           (addr >= UART_BASE && addr < UART_BASE + map::UART_SIZE) ||
           (addr >= TEST_BASE && addr < TEST_BASE + map::TEST_SIZE);
  }
  uint32_t read(uint32_t addr) const {
    if (is_ram(addr)) {
      const uint32_t off = addr - RAM_BASE;
      return ram[off] | (uint32_t)ram[off + 1] << 8 |
             (uint32_t)ram[off + 2] << 16 | (uint32_t)ram[off + 3] << 24;
    }
    if (addr == UART_BASE + 4) return 0x60u << 8;  // LSR at byte offset 5
    return 0;
  }
  void write(uint32_t addr, uint32_t data, uint8_t strb) {
    if (is_ram(addr)) {
      const uint32_t off = addr - RAM_BASE;
      for (int i = 0; i < 4; ++i)
        if (strb & (1u << i)) ram[off + i] = (uint8_t)(data >> (i * 8));
      if (tohost_en && addr == tohost_addr && strb == 0xf && data != 0) {
        exit_req = true;
        exit_code = data == 1 ? 0 : (int)(data >> 1);
        if (data != 1 && exit_code == 0) exit_code = 1;
      }
    } else if (addr == UART_BASE && (strb & 1)) {
      fputc(data & 0xff, stdout);
      fflush(stdout);
    } else if (addr >= TEST_BASE && addr < TEST_BASE + map::TEST_SIZE) {
      const uint32_t status = data & 0xffff;
      if (status == 0x5555 || status == 0x7777) {
        exit_req = true;
        exit_code = 0;
      } else if (status == 0x3333) {
        exit_req = true;
        exit_code = (int)(data >> 16);
        if (!exit_code) exit_code = 1;
      }
    }
  }
};

struct Event {
  bool valid, trap, rd_we;
  uint32_t pc, insn, cause, tval, rd, rd_val;
};

static bool load_program(const char* path, Bus& bus, uint32_t& reset_pc) {
  if (is_elf(path)) {
    ElfInfo info;
    if (!load_elf(path, bus, info)) return false;
    reset_pc = info.entry;
    bus.tohost_en = info.has_tohost;
    bus.tohost_addr = info.tohost;
    return true;
  }
  FILE* f = fopen(path, "rb");
  if (!f) { perror(path); return false; }
  fseek(f, 0, SEEK_END);
  const long len = ftell(f);
  fseek(f, 0, SEEK_SET);
  std::vector<uint8_t> image(len > 0 ? (size_t)len : 0);
  const bool ok = image.empty() ||
      fread(image.data(), 1, image.size(), f) == image.size();
  fclose(f);
  if (!ok) { fprintf(stderr, "[cosim] short read: %s\n", path); return false; }
  if (!bus.load_image(image.data(), image.size(), reset_pc)) {
    fprintf(stderr, "[cosim] image does not fit at 0x%08x\n", reset_pc);
    return false;
  }
  return true;
}

static bool mismatch(uint64_t event, const char* field, uint32_t got,
                     uint32_t expected) {
  fprintf(stderr, "[cosim] DIVERGENCE event=%llu %s: rtl=%08x iss=%08x\n",
          (unsigned long long)event, field, got, expected);
  return false;
}

static bool compare_event(uint64_t n, const Event& rtl, Cpu& iss,
                          Vaxcore* top) {
  if (iss.pc != rtl.pc) return mismatch(n, "pc before step", rtl.pc, iss.pc);
  const Stop stop = iss.step();
  const StepTrace& ref = iss.last;
  if (!ref.valid || ref.pc != rtl.pc) return mismatch(n, "pc", rtl.pc, ref.pc);
  if (ref.insn != rtl.insn) return mismatch(n, "insn", rtl.insn, ref.insn);
  if (rtl.trap != ref.trap)
    return mismatch(n, "trap", rtl.trap, ref.trap);
  if (rtl.trap && (rtl.cause != ref.cause || rtl.tval != ref.tval)) {
    if (rtl.cause != ref.cause) return mismatch(n, "trap cause", rtl.cause, ref.cause);
    return mismatch(n, "trap tval", rtl.tval, ref.tval);
  }
  if (rtl.rd_we != ref.rd_we) return mismatch(n, "rd write enable", rtl.rd_we, ref.rd_we);
  if (rtl.rd_we && (rtl.rd != ref.rd || rtl.rd_val != ref.rd_val)) {
    if (rtl.rd != ref.rd) return mismatch(n, "rd", rtl.rd, ref.rd);
    return mismatch(n, "rd value", rtl.rd_val, ref.rd_val);
  }
  if (top->trace_mstatus != iss.csr.mstatus)
    return mismatch(n, "mstatus", top->trace_mstatus, iss.csr.mstatus);
  if (top->trace_mtvec != iss.csr.mtvec)
    return mismatch(n, "mtvec", top->trace_mtvec, iss.csr.mtvec);
  if (top->trace_mepc != iss.csr.mepc)
    return mismatch(n, "mepc", top->trace_mepc, iss.csr.mepc);
  if (top->trace_mcause != iss.csr.mcause)
    return mismatch(n, "mcause", top->trace_mcause, iss.csr.mcause);
  if (top->trace_mtval != iss.csr.mtval)
    return mismatch(n, "mtval", top->trace_mtval, iss.csr.mtval);
  if (top->trace_mscratch != iss.csr.mscratch)
    return mismatch(n, "mscratch", top->trace_mscratch, iss.csr.mscratch);
  if (top->trace_mie != iss.csr.mie)
    return mismatch(n, "mie", top->trace_mie, iss.csr.mie);
  if (top->trace_mip != iss.csr.mip)
    return mismatch(n, "mip", top->trace_mip, iss.csr.mip);
  if (stop == Stop::Fault) {
    fprintf(stderr, "[cosim] ISS stopped after matching event %llu\n",
            (unsigned long long)n);
    return false;
  }
  return true;
}

int main(int argc, char** argv) {
  Verilated::commandArgs(argc, argv);
  const char* program = nullptr;
  int waitstates = 0;
  uint64_t max_cycles = 2000000;
  bool trace = false;
  for (int i = 1; i < argc; ++i) {
    if (!strcmp(argv[i], "--bin") && i + 1 < argc) program = argv[++i];
    else if (!strcmp(argv[i], "--ws") && i + 1 < argc) waitstates = atoi(argv[++i]);
    else if (!strcmp(argv[i], "--max") && i + 1 < argc)
      max_cycles = strtoull(argv[++i], nullptr, 0);
    else if (!strcmp(argv[i], "--trace")) trace = true;
    else { fprintf(stderr, "usage: %s --bin PROGRAM [--ws N] [--max CYCLES] [--trace]\n", argv[0]); return 1; }
  }
  if (!program) { fprintf(stderr, "[cosim] --bin is required\n"); return 1; }

  uint32_t reset_pc = RAM_BASE;
  Bus iss_bus(RAM_SIZE);
  iss_bus.uart_echo = false;
  if (!load_program(program, iss_bus, reset_pc)) return 1;
  RtlBus rtl_bus;
  rtl_bus.ram = iss_bus.ram_image();
  rtl_bus.tohost_en = iss_bus.tohost_en;
  rtl_bus.tohost_addr = iss_bus.tohost_addr;
  Cpu iss(iss_bus, reset_pc);

  Vaxcore* top = new Vaxcore;
  int icnt = 0, dcnt = 0;
  top->rst = 1; top->clk = 0; top->ibus_ready = 0; top->dbus_ready = 0;
  top->ibus_rdata = 0; top->dbus_rdata = 0; top->ibus_err = 0; top->dbus_err = 0;
  top->irq_software = 0; top->irq_timer = 0; top->irq_external = 0;
  top->eval(); top->clk = 1; top->eval(); top->clk = 0; top->rst = 0; top->eval();

  uint64_t cycles = 0, events = 0;
  bool ok = true;
  for (; cycles < max_cycles && !rtl_bus.exit_req; ++cycles) {
    top->clk = 0; top->eval();
    for (int settle = 0; settle < 3; ++settle) {
      top->ibus_ready = top->ibus_valid && icnt >= waitstates;
      top->ibus_rdata = rtl_bus.read(top->ibus_addr);
      top->ibus_err = top->ibus_valid && !rtl_bus.mapped(top->ibus_addr);
      top->dbus_ready = top->dbus_valid && dcnt >= waitstates;
      top->dbus_rdata = rtl_bus.read(top->dbus_addr);
      top->dbus_err = top->dbus_valid && !rtl_bus.mapped(top->dbus_addr);
      top->eval();
    }
    const bool icomp = top->ibus_valid && top->ibus_ready;
    const bool dcomp = top->dbus_valid && top->dbus_ready;
    const bool ivalid = top->ibus_valid, dvalid = top->dbus_valid;
    const uint32_t daddr = top->dbus_addr, dwdata = top->dbus_wdata;
    const uint8_t dwstrb = top->dbus_wstrb;
    const bool derr = top->dbus_err;
    const Event event = {bool(top->trace_valid), bool(top->trace_trap),
                         bool(top->trace_rd_we), top->trace_pc, top->trace_insn,
                         top->trace_cause, top->trace_tval, top->trace_rd,
                         top->trace_rd_val};

    top->clk = 1; top->eval();
    if (dcomp && dwstrb && !derr) rtl_bus.write(daddr, dwdata, dwstrb);
    icnt = icomp ? 0 : (ivalid ? icnt + 1 : 0);
    dcnt = dcomp ? 0 : (dvalid ? dcnt + 1 : 0);

    if (event.valid) {
      ++events;
      if (trace) fprintf(stderr, "%8llu %08x: %08x%s\n",
                         (unsigned long long)events, event.pc, event.insn,
                         event.trap ? " TRAP" : "");
      ok = compare_event(events, event, iss, top);
      if (!ok) break;
      if (dcomp && dwstrb && !derr && rtl_bus.is_ram(daddr)) {
        uint32_t expected = 0;
        if (!iss_bus.read(daddr, 4, expected) || rtl_bus.read(daddr) != expected) {
          ok = mismatch(events, "store memory", rtl_bus.read(daddr), expected);
          break;
        }
      }
    }
  }

  delete top;
  if (!ok) return 2;
  if (!rtl_bus.exit_req) {
    fprintf(stderr, "[cosim] TIMEOUT after %llu cycles (%llu events)\n",
            (unsigned long long)cycles, (unsigned long long)events);
    return 124;
  }
  if (iss_bus.exit_req != rtl_bus.exit_req || iss_bus.exit_code != rtl_bus.exit_code) {
    fprintf(stderr, "[cosim] DIVERGENCE finisher: rtl=%d iss=%d\n",
            rtl_bus.exit_code, iss_bus.exit_code);
    return 2;
  }
  fprintf(stderr, "[cosim] exit %d (%llu events, %llu cycles, ws=%d)\n",
          rtl_bus.exit_code, (unsigned long long)events,
          (unsigned long long)cycles, waitstates);
  return rtl_bus.exit_code;
}
