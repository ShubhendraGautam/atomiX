#include <stdint.h>

#include "clint.h"
#include "csr.h"
#include "platform.h"

extern void trap_entry(void);

static volatile uint32_t ticks;
enum { TIMER_INTERVAL = 10000, TIMER_IRQ = 0x80000007u };

void machine_trap(void) {
  if (csr_read_mcause() != TIMER_IRQ) test_finish(1);
  clint_schedule_after(TIMER_INTERVAL);
  uart_putchar('T');
  if (++ticks == 3) {
    uart_putchar('\n');
    test_finish(0);
  }
}

int main(void) {
  uart_puts("timer demo: ");
  csr_write_mtvec((uint32_t)(uintptr_t)trap_entry);
  clint_schedule_after(TIMER_INTERVAL);
  csr_set_mie(1u << 7);       // MTIE
  csr_set_mstatus(1u << 3);   // MIE
  for (;;) __asm__ volatile("wfi");
}
