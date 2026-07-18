#include "platform.h"
#include "spi.h"

int main(void) {
  SPI_CLKDIV = 0;
  spi_select(1);
  // The generic SoC runner leaves MISO high, just like an idle SD card.
  if (spi_transfer(0xa5) != 0xff) test_finish(1);
  spi_select(0);
  uart_puts("spi: PASS\n");
  test_finish(0);
}
