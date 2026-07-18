#include <stdint.h>

#include "clint.h"
#include "csr.h"
#include "platform.h"

extern void trap_entry(void);

// traps.S reserves 128 bytes = 32 words below a task's live stack pointer.
// It saves every GPR other than sp there, then machine_trap returns the frame
// that it should restore before mret.
enum { FRAME_WORDS = 32, STACK_WORDS = 256, TIMER_INTERVAL = 2000,
       TIMER_IRQ = 0x80000007u, SWITCHES = 6 };

struct task {
  uint32_t *frame;
  uint32_t mepc;
};

static uint32_t task_b_stack[STACK_WORDS];
static struct task tasks[2];
static volatile uint32_t current;
static volatile uint32_t ticks;
static volatile uint32_t epoch[2] = {1, 0};

static __attribute__((noreturn)) void task_a(void) {
  uint32_t seen = 0;
  for (;;) {
    const uint32_t now = epoch[0];
    if (now != seen) {
      uart_putchar('A');
      seen = now;
    }
  }
}

static __attribute__((noreturn)) void task_b(void) {
  uint32_t seen = 0;
  for (;;) {
    const uint32_t now = epoch[1];
    if (now != seen) {
      uart_putchar('B');
      seen = now;
    }
  }
}

uint32_t *machine_trap(uint32_t *frame) {
  if (csr_read_mcause() != TIMER_IRQ) test_finish(1);

  tasks[current].frame = frame;
  tasks[current].mepc = csr_read_mepc();
  clint_schedule_after(TIMER_INTERVAL);

  if (++ticks == SWITCHES) {
    uart_putchar('\n');
    test_finish(0);
  }

  current ^= 1;
  ++epoch[current];
  csr_write_mepc(tasks[current].mepc);
  return tasks[current].frame;
}

int main(void) {
  uart_puts("preempt: ");
  tasks[1].frame = task_b_stack + STACK_WORDS - FRAME_WORDS;
  tasks[1].mepc = (uint32_t)(uintptr_t)task_b;

  csr_write_mtvec((uint32_t)(uintptr_t)trap_entry);
  clint_schedule_after(TIMER_INTERVAL);
  csr_set_mie(1u << 7);       // MTIE
  csr_set_mstatus(1u << 3);   // MIE
  task_a();
}
