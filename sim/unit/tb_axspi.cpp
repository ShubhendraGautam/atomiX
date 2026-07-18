// Directed mode-0 SPI byte-transfer and register-contract test.
#include <cstdint>
#include <cstdio>

#include "Vaxspi.h"
#include "verilated.h"

static constexpr uint32_t kBase = 0x10010000u;
static constexpr uint32_t kData = kBase, kCtrl = kBase + 4,
                          kStatus = kBase + 8, kClkdiv = kBase + 12;
static int failures = 0;

static void check(bool condition, const char* description) {
  if (!condition) {
    std::fprintf(stderr, "FAIL: %s\n", description);
    failures++;
  }
}

static void tick(Vaxspi* top) {
  top->clk = 0;
  top->eval();
  top->clk = 1;
  top->eval();
  top->clk = 0;
  top->eval();
}

static void write_d(Vaxspi* top, uint32_t address, uint32_t value) {
  top->d_addr = address;
  top->d_wdata = value;
  top->d_wstrb = 0xf;
  top->d_valid = 1;
  top->eval();
  check(top->d_ready && !top->d_err, "SPI write completes without error");
  tick(top);
  top->d_valid = 0;
  top->eval();
}

static uint32_t read_d(Vaxspi* top, uint32_t address) {
  top->d_addr = address;
  top->d_wstrb = 0;
  top->d_valid = 1;
  top->eval();
  check(top->d_ready && !top->d_err, "SPI read completes without error");
  const uint32_t result = top->d_rdata;
  tick(top);
  top->d_valid = 0;
  top->eval();
  return result;
}

static uint32_t status_now(Vaxspi* top) {
  top->d_addr = kStatus;
  top->d_wstrb = 0;
  top->d_valid = 0;
  top->eval();
  return top->d_rdata;
}

int main(int argc, char** argv) {
  Verilated::commandArgs(argc, argv);
  Vaxspi top;
  top.clk = 0;
  top.rst = 1;
  top.i_valid = top.d_valid = 0;
  top.i_addr = top.d_addr = 0;
  top.i_wdata = top.d_wdata = 0;
  top.i_wstrb = top.d_wstrb = 0;
  top.spi_miso = 1;
  tick(&top);
  top.rst = 0;
  top.eval();

  check(top.spi_cs_n && !top.spi_sclk, "reset deasserts CS and holds SCLK low");
  write_d(&top, kClkdiv, 0);
  write_d(&top, kCtrl, 0);  // select card, no transfer
  check(!top.spi_cs_n, "software controls chip select");
  write_d(&top, kData, 0xa5);
  write_d(&top, kCtrl, 1);  // GO while keeping CS selected
  check(status_now(&top) & 1, "GO makes controller busy");

  const uint8_t received = 0x3cu;
  uint8_t sent = 0;
  int sample_count = 0;
  for (int guard = 0; (status_now(&top) & 1) && guard < 32; ++guard) {
    const bool rising_next = !top.spi_sclk;
    if (rising_next) {
      top.spi_miso = (received >> (7 - sample_count)) & 1;
      sent = uint8_t((sent << 1) | top.spi_mosi);
    }
    tick(&top);
    if (rising_next) sample_count++;
  }
  check(sample_count == 8, "SPI transfer has eight sampling edges");
  check(sent == 0xa5, "MOSI is MSB-first and stable at sampling edges");
  const uint32_t status = read_d(&top, kStatus);
  check(!(status & 1) && (status & 2), "completed byte sets RX valid and clears busy");
  const uint32_t captured = read_d(&top, kData);
  if (captured != received)
    std::fprintf(stderr, "MISO got %02x want %02x\n", captured, received);
  check(captured == received, "MISO is captured MSB-first");
  check(!(read_d(&top, kStatus) & 2), "reading DATA consumes RX valid");

  write_d(&top, kCtrl, 2);  // deselect card
  check(top.spi_cs_n, "software can release chip select");
  top.d_addr = kBase + 16;
  top.d_wstrb = 0;
  top.d_valid = 1;
  top.eval();
  check(top.d_ready && top.d_err, "unknown register reports access error");

  if (failures) {
    std::fprintf(stderr, "tb_axspi: %d FAILURE(S)\n", failures);
    return 1;
  }
  std::puts("tb_axspi: PASS");
  return 0;
}
