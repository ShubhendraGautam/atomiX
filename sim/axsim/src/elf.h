#pragma once
#include <cstdint>
#include "bus.h"

// Minimal ELF32 loader (no libelf dependency): loads PT_LOAD segments into
// the bus, returns the entry point, and looks up the `tohost` symbol that
// riscv-tests use for their HTIF exit protocol.
struct ElfInfo {
  uint32_t entry = 0;
  bool has_tohost = false;
  uint32_t tohost = 0;
};

// Returns false (with a diagnostic on stderr) on malformed/unloadable files.
bool load_elf(const char* path, Bus& bus, ElfInfo& info);

// True if the file starts with the ELF magic.
bool is_elf(const char* path);
