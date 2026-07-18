#include <stdint.h>

#include "fs.h"
#include "platform.h"
#include "sd.h"

struct fs_entry {
  char name[16];
  uint32_t block;
  uint32_t length;
};

static uint8_t sector[512];
#ifndef AXFS_BLOCK
#define AXFS_BLOCK 0u
#endif
static struct fs_entry entries[8];
static uint32_t entry_count;
static int mounted;

static uint32_t read_u32(const uint8_t *p) {
  return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
         ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static int same(const char *a, const char *b) {
  while (*a && *a == *b) { ++a; ++b; }
  return *a == *b;
}

int fs_mount(void) {
  if (sd_init() || sd_read_block(AXFS_BLOCK, sector)) return -1;
  if (sector[0] != 'A' || sector[1] != 'X' || sector[2] != 'F' ||
      sector[3] != 'S' || sector[4] != 1 || sector[5] > 8) return -1;
  entry_count = sector[5];
  for (uint32_t i = 0; i < entry_count; ++i) {
    const uint8_t *const disk = &sector[8 + i * 24];
    for (uint32_t j = 0; j < 16; ++j) entries[i].name[j] = (char)disk[j];
    entries[i].name[15] = 0;
    entries[i].block = read_u32(disk + 16);
    entries[i].length = read_u32(disk + 20);
    if (entries[i].length > 512) return -1;
  }
  mounted = 1;
  return 0;
}

void fs_list(void) {
  for (uint32_t i = 0; i < entry_count; ++i) {
    uart_puts(entries[i].name);
    uart_puts("\n");
  }
}

int fs_cat(const char *name) {
  if (!mounted) return -1;
  for (uint32_t i = 0; i < entry_count; ++i) {
    if (!same(name, entries[i].name)) continue;
    if (sd_read_block(entries[i].block, sector)) return -1;
    for (uint32_t j = 0; j < entries[i].length; ++j) uart_putchar((char)sector[j]);
    return 0;
  }
  return -1;
}
