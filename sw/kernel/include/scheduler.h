#pragma once

#include <stdint.h>

#include "task.h"

/* Return a runnable task slot, or TASK_NONE when no task can run.  The kernel
 * saves/restores contexts and decides when to invoke this policy; a scheduler
 * component only makes the selection. */
uint32_t scheduler_select(const struct task *tasks, uint32_t task_slots,
                          uint32_t current_task);
