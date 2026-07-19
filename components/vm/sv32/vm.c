#include <stdint.h>

#include "page.h"
#include "platform.h"
#include "task.h"
#include "vm.h"

enum {
  PTE_V = 1u << 0,
  PTE_R = 1u << 1,
  PTE_W = 1u << 2,
  PTE_X = 1u << 3,
  PTE_U = 1u << 4,
  PTE_A = 1u << 6,
  PTE_D = 1u << 7,
  SATP_MODE_SV32 = 1u << 31,
  USER_CODE_VA = 0x40000000u,
  USER_STACK_VA = USER_CODE_VA + PAGE_SIZE,
};

extern void user_entry(void);

static uint32_t pte_leaf(uint32_t physical, uint32_t flags) {
  return ((physical >> 12) << 10) | flags;
}

static uint32_t pte_pointer(uint32_t physical) {
  return ((physical >> 12) << 10) | PTE_V;
}

static void zero_page(uint32_t *page) {
  for (uint32_t i = 0; i < PAGE_SIZE / sizeof(*page); ++i) page[i] = 0;
}

static void copy_page(uint32_t *dst, const uint32_t *src) {
  for (uint32_t i = 0; i < PAGE_SIZE / sizeof(*dst); ++i) dst[i] = src[i];
}

void vm_bootstrap_map(volatile uint32_t *root_pt, volatile uint32_t *low_pt) {
  const uint32_t kernel_flags = PTE_V | PTE_R | PTE_W | PTE_X | PTE_A | PTE_D;
  const uint32_t device_flags = PTE_V | PTE_R | PTE_W | PTE_A | PTE_D;

  /* Kernel RAM, UART, and CLINT are superpages. The finisher uses one L0 PTE. */
  root_pt[0x200] = pte_leaf(0x80000000u, kernel_flags);
  root_pt[0x040] = pte_leaf(AX_UART_BASE, device_flags);
  root_pt[0x008] = pte_leaf(AX_CLINT_BASE, device_flags);
  /* Accelerator role window: a 4 MiB device superpage identity-maps the fixed
   * 64 KiB window so the S-mode shell control plane can drive the role.  This
   * VA index (0x100) also names USER_CODE_VA, but user address spaces overwrite
   * it with their code mapping, so kernel and user views never collide. */
  root_pt[AX_ROLE_BASE >> 22] = pte_leaf(AX_ROLE_BASE, device_flags);
  root_pt[0x000] = pte_pointer((uint32_t)(uintptr_t)low_pt);
  low_pt[(AX_TEST_BASE >> 12) & 0x3ffu] = pte_leaf(AX_TEST_BASE, device_flags);
}

uint32_t vm_root_satp(const volatile uint32_t *root_pt) {
  return SATP_MODE_SV32 | ((uint32_t)(uintptr_t)root_pt >> 12);
}

int vm_create_user_space(struct task *task, const volatile uint32_t *root_pt) {
  uint32_t *const page_root = page_alloc();
  uint32_t *const user_pt = page_alloc();
  uint32_t *const user_stack = page_alloc();
  if (page_root == 0 || user_pt == 0 || user_stack == 0) {
    if (page_root != 0) page_free(page_root);
    if (user_pt != 0) page_free(user_pt);
    if (user_stack != 0) page_free(user_stack);
    return -1;
  }
  zero_page(page_root);
  zero_page(user_pt);
  for (uint32_t i = 0; i < 1024u; ++i) page_root[i] = root_pt[i];
  page_root[USER_CODE_VA >> 22] = pte_pointer((uint32_t)(uintptr_t)user_pt);
  user_pt[0] = pte_leaf((uint32_t)(uintptr_t)user_entry & ~(PAGE_SIZE - 1u),
                        PTE_V | PTE_R | PTE_X | PTE_U | PTE_A);
  user_pt[1] = pte_leaf((uint32_t)(uintptr_t)user_stack,
                        PTE_V | PTE_R | PTE_W | PTE_U | PTE_A | PTE_D);
  task->page_root = page_root;
  task->user_pt = user_pt;
  task->user_stack = user_stack;
  task->satp = vm_root_satp(page_root);
  return 0;
}

int vm_clone_user_space(struct task *child, const struct task *parent) {
  uint32_t *const page_root = page_alloc();
  uint32_t *const user_pt = page_alloc();
  uint32_t *const user_stack = page_alloc();
  if (page_root == 0 || user_pt == 0 || user_stack == 0) {
    if (page_root != 0) page_free(page_root);
    if (user_pt != 0) page_free(user_pt);
    if (user_stack != 0) page_free(user_stack);
    return -1;
  }
  copy_page(page_root, parent->page_root);
  copy_page(user_pt, parent->user_pt);
  copy_page(user_stack, parent->user_stack);
  user_pt[1] = pte_leaf((uint32_t)(uintptr_t)user_stack,
                        PTE_V | PTE_R | PTE_W | PTE_U | PTE_A | PTE_D);
  page_root[USER_CODE_VA >> 22] = pte_pointer((uint32_t)(uintptr_t)user_pt);
  child->page_root = page_root;
  child->user_pt = user_pt;
  child->user_stack = user_stack;
  child->satp = vm_root_satp(page_root);
  return 0;
}

void vm_destroy_user_space(struct task *task) {
  if (task->page_root != 0) page_free(task->page_root);
  if (task->user_pt != 0) page_free(task->user_pt);
  if (task->user_stack != 0) page_free(task->user_stack);
  task->page_root = 0;
  task->user_pt = 0;
  task->user_stack = 0;
  task->satp = 0;
}
