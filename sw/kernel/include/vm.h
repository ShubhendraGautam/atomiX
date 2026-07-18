#pragma once

#include <stdint.h>

#include "task.h"

/* The selected VM component owns bootstrap mappings and the user portion of
 * a task address space.  Return zero on success and non-zero on allocation or
 * policy failure. Kernel stacks and trap frames deliberately remain kernel
 * responsibilities. */
void vm_bootstrap_map(volatile uint32_t *root_pt, volatile uint32_t *low_pt);
uint32_t vm_root_satp(const volatile uint32_t *root_pt);
int vm_create_user_space(struct task *task, const volatile uint32_t *root_pt);
int vm_clone_user_space(struct task *child, const struct task *parent);
void vm_destroy_user_space(struct task *task);
