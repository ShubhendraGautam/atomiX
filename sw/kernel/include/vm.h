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

/* Translate a user virtual address in `task` to a pointer the kernel can
 * dereference, or 0 if the address is not mapped, not user-accessible, or not
 * writable when `write` is requested.
 *
 * The kernel needs this because sstatus.SUM is deliberately left clear: S-mode
 * cannot dereference user addresses directly, so a syscall that takes a pointer
 * must translate it and must be able to *fail* translating it.  A user program
 * passing a garbage pointer to write() has to become -EFAULT, not a supervisor
 * page fault -- unprivileged code must not be able to fault the kernel. */
void *vm_translate_user(const struct task *task, uint32_t user_va, int write);

/* Permissions for vm_map_user_page, kept independent of the PTE encoding so a
 * loader (or any other caller) does not have to know Sv32's bit layout. */
enum {
  VM_R = 1u << 0,
  VM_W = 1u << 1,
  VM_X = 1u << 2,
};

/* Create a user address space with the kernel mappings in place but no user
 * pages, for a caller that will place them itself.  `vm_create_user_space`
 * remains the fixed two-page layout the built-in payload uses. */
int vm_create_empty_user_space(struct task *task, const volatile uint32_t *root_pt);

/* Map one physical page at `user_va` with `perms` (a VM_* mask).  The page must
 * come from page_alloc(); the address space takes ownership.  Returns zero on
 * success.
 *
 * Only the 4 MiB region starting at USER_CODE_VA is mappable: one L0 table
 * covers it, which is ample for the images this kernel loads and keeps the
 * address space one page of tables rather than a tree to walk and free. */
int vm_map_user_page(struct task *task, uint32_t user_va, void *page,
                     uint32_t perms);

/* Unmap `user_va` and free its page if the address space owns it.  Safe to
 * call on an address that is not mapped. */
void vm_unmap_user_page(struct task *task, uint32_t user_va);
