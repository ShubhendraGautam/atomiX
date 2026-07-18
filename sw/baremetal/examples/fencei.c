#include <stdint.h>

#include "platform.h"

/* Keep the first instruction simple and patchable.  The first call brings
 * the original instruction into the I$; the second must observe the store
 * only after FENCE.I has invalidated it. */
__attribute__((naked, noinline)) static int patched_function(void) {
  __asm__ volatile (
      "li a0, 0\n"
      "ret\n");
}

int main(void) {
  volatile uint32_t *instruction =
      (volatile uint32_t *)(uintptr_t)&patched_function;

  if (patched_function() != 0) test_finish(1);
  *instruction = 0x00100513u;  /* addi a0, x0, 1 */
  __asm__ volatile ("fence.i" ::: "memory");
  if (patched_function() != 1) test_finish(2);

  uart_puts("fence.i: PASS\n");
  test_finish(0);
}
