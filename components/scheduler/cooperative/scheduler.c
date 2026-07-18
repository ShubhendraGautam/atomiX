#include <stdint.h>

#include "scheduler.h"

/* A useful alternative policy for deterministic bring-up: keep a runnable
 * task running across timer ticks, but hand control to the first runnable task
 * when the current task blocks or exits. */
uint32_t scheduler_select(const struct task *tasks, uint32_t task_slots,
                          uint32_t current_task) {
  if (current_task < task_slots &&
      tasks[current_task].state == TASK_RUNNABLE) return current_task;
  for (uint32_t i = 0; i < task_slots; ++i)
    if (tasks[i].state == TASK_RUNNABLE) return i;
  return TASK_NONE;
}
