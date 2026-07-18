#include <stdint.h>

#include "scheduler.h"

/* Reference preemptive policy: begin after the previously running slot and
 * choose the first runnable task. Context management stays in kernel.c. */
uint32_t scheduler_select(const struct task *tasks, uint32_t task_slots,
                          uint32_t current_task) {
  const uint32_t start = current_task == TASK_NONE ? 0u :
      (current_task + 1u) % task_slots;
  for (uint32_t i = 0; i < task_slots; ++i) {
    const uint32_t candidate = (start + i) % task_slots;
    if (tasks[candidate].state == TASK_RUNNABLE) return candidate;
  }
  return TASK_NONE;
}
