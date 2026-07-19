/* Shell + role contract proof against role.loopback: discovery, descriptor
 * programming, doorbell, completion polling, and result readback all happen
 * through the fixed role window.  Runs on the RTL SoC only; the ISS does not
 * model the role device. */
#include "platform.h"
#include "role.h"

static uint32_t pattern(uint32_t i) { return 0xa5000000u + i * 0x01010101u; }

static void fail(unsigned code, const char *what) {
  uart_puts("role loopback: FAIL ");
  uart_puts(what);
  uart_puts("\n");
  test_finish(code);
}

static void run_copy(uint32_t src_word, uint32_t dst_word, uint32_t words,
                     uint32_t seed) {
  for (uint32_t i = 0; i < words; ++i)
    mmio_write32(AX_ROLE_LOOP_BUF + 4u * (src_word + i), pattern(seed + i));
  mmio_write32(AX_ROLE_LOOP_SRC, 4u * src_word);
  mmio_write32(AX_ROLE_LOOP_DST, 4u * dst_word);
  mmio_write32(AX_ROLE_LOOP_LEN, words);
  role_ring_doorbell();
  role_wait_done();
  for (uint32_t i = 0; i < words; ++i)
    if (mmio_read32(AX_ROLE_LOOP_BUF + 4u * (dst_word + i)) != pattern(seed + i))
      fail(3, "copied data mismatch");
}

int main(void) {
  if (role_id() != AX_ROLE_LOOP_ID) fail(1, "discovery: ROLE_ID mismatch");
  if (mmio_read32(AX_ROLE_VERSION) == 0) fail(2, "VERSION reads zero");

  run_copy(0u, 256u, 64u, 0u);
  if (mmio_read32(AX_ROLE_LOOP_COUNT) != 1u) fail(4, "COUNT after first job");

  /* Clear DONE (write-1-to-clear) and prove the engine reprograms. */
  mmio_write32(AX_ROLE_STATUS, AX_ROLE_STATUS_DONE);
  if (mmio_read32(AX_ROLE_STATUS) & AX_ROLE_STATUS_DONE)
    fail(5, "DONE did not clear");
  run_copy(128u, 512u, 32u, 0x40u);
  if (mmio_read32(AX_ROLE_LOOP_COUNT) != 2u) fail(6, "COUNT after second job");

  uart_puts("role loopback: PASS\n");
  test_finish(0);
}
