#include "elf.h"

#include <cstdio>
#include <cstring>
#include <vector>

namespace {

bool read_file(const char* path, std::vector<uint8_t>& out) {
  FILE* f = fopen(path, "rb");
  if (!f) {
    perror(path);
    return false;
  }
  fseek(f, 0, SEEK_END);
  const long len = ftell(f);
  fseek(f, 0, SEEK_SET);
  out.resize(len > 0 ? (size_t)len : 0);
  const bool ok =
      out.empty() || fread(out.data(), 1, out.size(), f) == out.size();
  fclose(f);
  if (!ok) fprintf(stderr, "[axsim] short read: %s\n", path);
  return ok;
}

uint32_t u32(const std::vector<uint8_t>& b, size_t off) {
  return (uint32_t)b[off] | (uint32_t)b[off + 1] << 8 |
         (uint32_t)b[off + 2] << 16 | (uint32_t)b[off + 3] << 24;
}
uint16_t u16(const std::vector<uint8_t>& b, size_t off) {
  return (uint16_t)(b[off] | b[off + 1] << 8);
}

bool err(const char* path, const char* what) {
  fprintf(stderr, "[axsim] %s: %s\n", path, what);
  return false;
}

}  // namespace

bool is_elf(const char* path) {
  FILE* f = fopen(path, "rb");
  if (!f) return false;
  uint8_t magic[4] = {};
  const size_t n = fread(magic, 1, 4, f);
  fclose(f);
  return n == 4 && !memcmp(magic, "\x7f""ELF", 4);
}

bool load_elf(const char* path, Bus& bus, ElfInfo& info) {
  std::vector<uint8_t> b;
  if (!read_file(path, b)) return false;
  if (b.size() < 52 || memcmp(b.data(), "\x7f""ELF", 4))
    return err(path, "not an ELF file");
  if (b[4] != 1 || b[5] != 1)
    return err(path, "not a little-endian ELF32 file");
  if (u16(b, 18) != 243) return err(path, "not a RISC-V ELF (e_machine)");

  info.entry = u32(b, 24);

  // Program headers: load PT_LOAD segments at their physical address
  // (riscv-tests and our software link with vaddr == paddr).
  const uint32_t phoff = u32(b, 28);
  const uint16_t phentsize = u16(b, 42), phnum = u16(b, 44);
  for (uint16_t i = 0; i < phnum; i++) {
    const size_t ph = phoff + (size_t)i * phentsize;
    if (ph + 32 > b.size()) return err(path, "truncated program header");
    if (u32(b, ph) != 1) continue;  // PT_LOAD
    const uint32_t off = u32(b, ph + 4), paddr = u32(b, ph + 12);
    const uint32_t filesz = u32(b, ph + 16), memsz = u32(b, ph + 20);
    if (memsz == 0) continue;
    if (off + filesz > b.size()) return err(path, "segment past end of file");
    std::vector<uint8_t> seg(memsz, 0);  // filesz..memsz zero-fills (.bss)
    memcpy(seg.data(), &b[off], filesz);
    if (!bus.load_image(seg.data(), seg.size(), paddr)) {
      fprintf(stderr, "[axsim] %s: segment does not fit at 0x%08x (%u bytes)\n",
              path, paddr, memsz);
      return false;
    }
  }

  // Symbol tables: find `tohost` for the riscv-tests HTIF exit protocol.
  const uint32_t shoff = u32(b, 32);
  const uint16_t shentsize = u16(b, 46), shnum = u16(b, 48);
  for (uint16_t i = 0; i < shnum && !info.has_tohost; i++) {
    const size_t sh = shoff + (size_t)i * shentsize;
    if (sh + 40 > b.size()) break;
    if (u32(b, sh + 4) != 2) continue;  // SHT_SYMTAB
    const uint32_t symoff = u32(b, sh + 16), symsz = u32(b, sh + 20);
    const uint32_t entsz = u32(b, sh + 36), strndx = u32(b, sh + 24);
    const size_t str_sh = shoff + (size_t)strndx * shentsize;
    if (entsz < 16 || str_sh + 40 > b.size()) continue;
    const uint32_t stroff = u32(b, str_sh + 16), strsz = u32(b, str_sh + 20);
    for (uint32_t s = 0; s + entsz <= symsz; s += entsz) {
      const size_t sym = symoff + s;
      if (sym + 16 > b.size()) break;
      const uint32_t name = u32(b, sym);
      if (name + 7 <= strsz && stroff + name + 7 <= b.size() &&
          !memcmp(&b[stroff + name], "tohost", 7)) {
        info.has_tohost = true;
        info.tohost = u32(b, sym + 4);
        break;
      }
    }
  }
  return true;
}
