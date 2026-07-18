#include <stdint.h>

#include "platform.h"
#include "sd.h"

#if AXOS_SD
enum { SPI_GO = 1u, SPI_CS_N = 2u, SPI_BUSY = 1u };

static void select_card(int selected) {
  mmio_write32(AX_SPI_BASE + 4u, selected ? 0u : SPI_CS_N);
}

static uint8_t transfer(uint8_t value) {
  mmio_write32(AX_SPI_BASE, value);
  mmio_write32(AX_SPI_BASE + 4u, SPI_GO);
  while (mmio_read32(AX_SPI_BASE + 8u) & SPI_BUSY) { }
  return (uint8_t)mmio_read32(AX_SPI_BASE);
}

static void deselect_card(void) { select_card(0); (void)transfer(0xff); }

static uint8_t wait_r1(void) {
  for (uint32_t i = 0; i < 16; ++i) {
    const uint8_t response = transfer(0xff);
    if (!(response & 0x80)) return response;
  }
  return 0xff;
}

static uint8_t command(uint8_t number, uint32_t argument, uint8_t crc) {
  select_card(1);
  (void)transfer(0x40u | number);
  (void)transfer((uint8_t)(argument >> 24));
  (void)transfer((uint8_t)(argument >> 16));
  (void)transfer((uint8_t)(argument >> 8));
  (void)transfer((uint8_t)argument);
  (void)transfer(crc);
  return wait_r1();
}

int sd_init(void) {
  mmio_write32(AX_SPI_BASE + 12u, 0);
  select_card(0);
  for (uint32_t i = 0; i < 10; ++i) (void)transfer(0xff);
  if (command(0, 0, 0x95) != 0x01) return -1;
  deselect_card();
  if (command(8, 0x1aa, 0x87) != 0x01) return -2;
  if (transfer(0xff) != 0 || transfer(0xff) != 0 ||
      transfer(0xff) != 1 || transfer(0xff) != 0xaa) return -3;
  deselect_card();
  for (uint32_t i = 0; i < 16; ++i) {
    if (command(55, 0, 1) > 1) return -4;
    deselect_card();
    if (command(41, 0x40000000u, 1) == 0) { deselect_card(); break; }
    deselect_card();
    if (i == 15) return -5;
  }
  if (command(58, 0, 1) != 0) return -6;
  const uint8_t ocr = transfer(0xff);
  (void)transfer(0xff); (void)transfer(0xff); (void)transfer(0xff);
  deselect_card();
  return (ocr & 0x40) ? 0 : -7;
}

int sd_read_block(uint32_t block, uint8_t *data) {
  if (command(17, block, 1) != 0) { deselect_card(); return -1; }
  uint8_t token = 0xff;
  for (uint32_t i = 0; i < 16 && token == 0xff; ++i) token = transfer(0xff);
  if (token != 0xfe) { deselect_card(); return -2; }
  for (uint32_t i = 0; i < 512; ++i) data[i] = transfer(0xff);
  (void)transfer(0xff); (void)transfer(0xff);
  deselect_card();
  return 0;
}
#else
int sd_init(void) { return -1; }
int sd_read_block(uint32_t block, uint8_t *data) {
  (void)block; (void)data;
  return -1;
}
#endif
