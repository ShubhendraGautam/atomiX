#pragma once

#include <stdint.h>

#define SPI_BASE 0x10010000u
#define SPI_DATA   (*(volatile uint32_t *)(SPI_BASE + 0x00))
#define SPI_CTRL   (*(volatile uint32_t *)(SPI_BASE + 0x04))
#define SPI_STATUS (*(volatile uint32_t *)(SPI_BASE + 0x08))
#define SPI_CLKDIV (*(volatile uint32_t *)(SPI_BASE + 0x0c))

enum { SPI_BUSY = 1u, SPI_RX_VALID = 2u, SPI_GO = 1u, SPI_CS_N = 2u };

static inline void spi_select(int selected) {
  SPI_CTRL = selected ? 0 : SPI_CS_N;
}

static inline uint8_t spi_transfer(uint8_t value) {
  SPI_DATA = value;
  SPI_CTRL = SPI_GO;  /* CS remains asserted from spi_select(1). */
  while (SPI_STATUS & SPI_BUSY) { }
  return (uint8_t)SPI_DATA;
}
