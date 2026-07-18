#pragma once
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

// Physical memory map — DESIGN.md §3.2, QEMU-virt aligned.
namespace map {
constexpr uint32_t ROM_BASE   = 0x00001000;
constexpr uint32_t ROM_SIZE   = 0x1000;
constexpr uint32_t TEST_BASE  = 0x00100000;  // QEMU sifive_test finisher
constexpr uint32_t TEST_SIZE  = 0x1000;
constexpr uint32_t CLINT_BASE = 0x02000000;
constexpr uint32_t CLINT_SIZE = 0x10000;
constexpr uint32_t UART_BASE  = 0x10000000;
constexpr uint32_t UART_SIZE  = 0x1000;
constexpr uint32_t RAM_BASE   = 0x80000000;
}  // namespace map

// System bus: routes CPU accesses to ROM/RAM/devices.
// Returns false on a decode miss (unmapped address) — the caller raises the
// access fault. Mirrors aXbus semantics: the RTL bus returns `err` for the
// same cases.
class Bus {
 public:
  explicit Bus(size_t ram_bytes) : ram(ram_bytes, 0) {}

  bool read(uint32_t addr, unsigned size, uint32_t& val) {
    if (const uint8_t* p = backing(addr, size)) {
      val = 0;
      for (unsigned i = 0; i < size; i++) val |= (uint32_t)p[i] << (8 * i);
      return true;
    }
    if (addr >= map::UART_BASE && addr < map::UART_BASE + map::UART_SIZE) {
      val = uart_read(addr - map::UART_BASE);
      return true;
    }
    if (addr >= map::CLINT_BASE && addr - map::CLINT_BASE < map::CLINT_SIZE)
      return clint_read(addr - map::CLINT_BASE, size, val);
    return false;
  }

  bool write(uint32_t addr, unsigned size, uint32_t val) {
    if (addr >= map::ROM_BASE && addr < map::ROM_BASE + map::ROM_SIZE)
      return false;  // ROM is read-only; store there = access fault
    if (uint8_t* p = backing(addr, size)) {
      for (unsigned i = 0; i < size; i++) p[i] = (uint8_t)(val >> (8 * i));
      // riscv-tests HTIF exit protocol: a nonzero word stored to the
      // `tohost` symbol ends the run — 1 = pass, (n<<1)|1 = fail test n.
      if (tohost_en && addr == tohost_addr && size == 4 && val != 0) {
        exit_req = true;
        exit_code = (val == 1) ? 0 : (int)(val >> 1);
        if (val != 1 && exit_code == 0) exit_code = 1;
      }
      return true;
    }
    if (addr >= map::UART_BASE && addr < map::UART_BASE + map::UART_SIZE) {
      uart_write(addr - map::UART_BASE, (uint8_t)val);
      return true;
    }
    if (addr >= map::CLINT_BASE && addr - map::CLINT_BASE < map::CLINT_SIZE)
      return clint_write(addr - map::CLINT_BASE, size, val);
    if (addr >= map::TEST_BASE && addr < map::TEST_BASE + map::TEST_SIZE) {
      // QEMU sifive_test protocol: 0x5555 = pass, 0x3333 | code<<16 = fail,
      // 0x7777 = reset (treated as a clean exit). Same binary exits
      // identically under QEMU -machine virt.
      const uint32_t status = val & 0xFFFF;
      if (status == 0x5555 || status == 0x7777) {
        exit_req = true;
        exit_code = 0;
      } else if (status == 0x3333) {
        exit_req = true;
        exit_code = (int)(val >> 16);
        if (exit_code == 0) exit_code = 1;
      }
      return true;
    }
    return false;
  }

  bool exit_req = false;  // set by a test-finisher or HTIF tohost write
  int exit_code = 0;
  bool tohost_en = false;  // watch tohost_addr for HTIF writes
  uint32_t tohost_addr = 0;
  bool uart_echo = true;   // false lets cosim print the RTL side only

  // One ISS time step per retired instruction. The precise delivery point is
  // defined in Cpu::step(); this exposes the same MSIP/MTIP sources as RTL.
  void tick() { ++mtime; }
  uint32_t mip() const {
    return (msip ? (1u << 3) : 0) | (mtime >= mtimecmp ? (1u << 7) : 0);
  }

  const std::vector<uint8_t>& ram_image() const { return ram; }

  // Direct image load (bypasses device decode); false if out of range.
  bool load_image(const uint8_t* data, size_t len, uint32_t addr) {
    uint8_t* p = backing(addr, len);
    if (!p) return false;
    memcpy(p, data, len);
    return true;
  }

 private:
  // Backing store for plain-memory ranges; nullptr for devices/unmapped.
  uint8_t* backing(uint32_t addr, size_t len) {
    if (addr >= map::RAM_BASE && addr - map::RAM_BASE + len <= ram.size())
      return &ram[addr - map::RAM_BASE];
    if (addr >= map::ROM_BASE && addr - map::ROM_BASE + len <= map::ROM_SIZE)
      return &rom[addr - map::ROM_BASE];
    return nullptr;
  }
  const uint8_t* backing(uint32_t addr, size_t len) const {
    return const_cast<Bus*>(this)->backing(addr, len);
  }

  // 16550 subset: THR (offset 0) transmits; LSR (offset 5) always reports
  // "transmitter empty". RX side arrives with the interactive console.
  void uart_write(uint32_t off, uint8_t b) {
    if (off == 0 && uart_echo) {
      fputc(b, stdout);
      fflush(stdout);
    }
  }
  uint32_t uart_read(uint32_t off) {
    if (off == 5) return 0x60;  // LSR: THR empty + transmitter idle
    return 0;
  }

  // QEMU-virt CLINT subset (hart 0): MSIP, MTIMECMP, and MTIME. The CPU
  // presents native byte addresses and sizes, unlike the aligned RTL bus
  // view, so byte extraction/merging belongs here.
  bool clint_read(uint32_t off, unsigned size, uint32_t& val) const {
    uint64_t value;
    uint32_t base;
    unsigned width;
    if (off < 0x0004) {
      value = msip; base = 0x0000; width = 4;
    } else if (off >= 0x4000 && off < 0x4008) {
      value = mtimecmp; base = 0x4000; width = 8;
    } else if (off >= 0xbff8 && off < 0xc000) {
      value = mtime; base = 0xbff8; width = 8;
    } else {
      return false;
    }
    if (size == 0 || size > 4 || off - base + size > width) return false;
    val = (uint32_t)(value >> (8 * (off - base)));
    if (size < 4) val &= (1u << (8 * size)) - 1;
    return true;
  }

  bool clint_write(uint32_t off, unsigned size, uint32_t val) {
    uint64_t* target;
    uint32_t base;
    unsigned width;
    uint64_t msip_value = msip;
    bool is_msip = false;
    if (off < 0x0004) {
      target = &msip_value; base = 0x0000; width = 4; is_msip = true;
    } else if (off >= 0x4000 && off < 0x4008) {
      target = &mtimecmp; base = 0x4000; width = 8;
    } else if (off >= 0xbff8 && off < 0xc000) {
      target = &mtime; base = 0xbff8; width = 8;
    } else {
      return false;
    }
    if (size == 0 || size > 4 || off - base + size > width) return false;
    const unsigned shift = 8 * (off - base);
    const uint64_t mask = (size == 4 ? 0xffffffffull :
                           (1ull << (8 * size)) - 1) << shift;
    *target = (*target & ~mask) | ((uint64_t)val << shift & mask);
    if (is_msip) msip = *target & 1;
    return true;
  }

  std::vector<uint8_t> ram;
  uint8_t rom[map::ROM_SIZE] = {};
  bool msip = false;
  uint64_t mtime = 0;
  uint64_t mtimecmp = ~0ull;
};
