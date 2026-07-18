#include <stdint.h>

#include "platform.h"
#include "spi.h"

static void sd_deselect(void) {
  spi_select(0);
  (void)spi_transfer(0xff);
}

static uint8_t sd_wait_r1(void) {
  for (unsigned i = 0; i < 16; ++i) {
    const uint8_t value = spi_transfer(0xff);
    if (!(value & 0x80)) return value;
  }
  return 0xff;
}

static uint8_t sd_command(uint8_t command, uint32_t argument, uint8_t crc) {
  spi_select(1);
  (void)spi_transfer(0x40 | command);
  (void)spi_transfer((uint8_t)(argument >> 24));
  (void)spi_transfer((uint8_t)(argument >> 16));
  (void)spi_transfer((uint8_t)(argument >> 8));
  (void)spi_transfer((uint8_t)argument);
  (void)spi_transfer(crc);
  return sd_wait_r1();
}

static int sd_init(void) {
  SPI_CLKDIV = 0;
  spi_select(0);
  for (unsigned i = 0; i < 10; ++i) (void)spi_transfer(0xff);

  if (sd_command(0, 0, 0x95) != 0x01) return -1;
  sd_deselect();
  if (sd_command(8, 0x1aa, 0x87) != 0x01) return -2;
  if (spi_transfer(0xff) != 0 || spi_transfer(0xff) != 0 ||
      spi_transfer(0xff) != 1 || spi_transfer(0xff) != 0xaa) return -3;
  sd_deselect();

  for (unsigned i = 0; i < 16; ++i) {
    if (sd_command(55, 0, 0x01) > 1) return -4;
    sd_deselect();
    if (sd_command(41, 0x40000000u, 0x01) == 0) {
      sd_deselect();
      break;
    }
    sd_deselect();
    if (i == 15) return -5;
  }
  if (sd_command(58, 0, 0x01) != 0) return -6;
  if (!(spi_transfer(0xff) & 0x40)) return -7;
  (void)spi_transfer(0xff); (void)spi_transfer(0xff); (void)spi_transfer(0xff);
  sd_deselect();
  return 0;
}

static int sd_read_block(uint32_t block, uint8_t *data) {
  if (sd_command(17, block, 0x01) != 0) {
    sd_deselect();
    return -1;
  }
  uint8_t token = 0xff;
  for (unsigned i = 0; i < 16 && token == 0xff; ++i) token = spi_transfer(0xff);
  if (token != 0xfe) {
    sd_deselect();
    return -2;
  }
  for (unsigned i = 0; i < 512; ++i) data[i] = spi_transfer(0xff);
  (void)spi_transfer(0xff); (void)spi_transfer(0xff);
  sd_deselect();
  return 0;
}

int main(void) {
  uint8_t block[512];
  if (sd_init() || sd_read_block(0, block)) test_finish(1);
  const char marker[] = "atomiX SD block 0";
  for (unsigned i = 0; i < sizeof(marker) - 1; ++i)
    if (block[i] != (uint8_t)marker[i]) test_finish(2);
  if (block[127] != 0x7f || block[511] != 0xff) test_finish(3);
  uart_puts("sd: PASS\n");
  test_finish(0);
}
