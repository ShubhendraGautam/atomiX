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
static uint8_t metadata[512];
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

static uint32_t length(const char *text) {
  uint32_t result = 0;
  while (text[result]) ++result;
  return result;
}

static void write_u32(uint8_t *p, uint32_t value) {
  p[0] = (uint8_t)value;
  p[1] = (uint8_t)(value >> 8);
  p[2] = (uint8_t)(value >> 16);
  p[3] = (uint8_t)(value >> 24);
}

static uint32_t next_free_block(void) {
  uint32_t result = AXFS_BLOCK + 1;
  for (uint32_t i = 0; i < entry_count; ++i) {
    const uint32_t end = entries[i].block + 1;  // AXFS v1 files are <= 512 B.
    if (end > result) result = end;
  }
  return result;
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

int fs_write(const char *name, const char *data) {
  if (!mounted) return -1;
  const uint32_t name_length = length(name);
  const uint32_t data_length = length(data);
  if (!name_length || name_length > 15 || data_length > 512) return -2;

  uint32_t index = entry_count;
  for (uint32_t i = 0; i < entry_count; ++i) {
    if (same(name, entries[i].name)) { index = i; break; }
  }
  if (index == entry_count && entry_count == 8) return -3;

  uint32_t block = index == entry_count ? next_free_block() : entries[index].block;
  for (uint32_t i = 0; i < 512; ++i) sector[i] = 0;
  for (uint32_t i = 0; i < data_length; ++i) sector[i] = (uint8_t)data[i];
  if (sd_write_block(block, sector)) return -4;

  if (sd_read_block(AXFS_BLOCK, metadata)) return -5;
  const uint32_t at = 8 + index * 24;
  for (uint32_t i = 0; i < 16; ++i) metadata[at + i] = 0;
  for (uint32_t i = 0; i < name_length; ++i) metadata[at + i] = (uint8_t)name[i];
  write_u32(&metadata[at + 16], block);
  write_u32(&metadata[at + 20], data_length);
  if (index == entry_count) metadata[5] = (uint8_t)(entry_count + 1);
  if (sd_write_block(AXFS_BLOCK, metadata)) return -6;

  for (uint32_t i = 0; i < 16; ++i) entries[index].name[i] = 0;
  for (uint32_t i = 0; i < name_length; ++i) entries[index].name[i] = name[i];
  entries[index].block = block;
  entries[index].length = data_length;
  if (index == entry_count) ++entry_count;
  return 0;
}
