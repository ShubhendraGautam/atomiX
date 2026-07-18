#include <stdint.h>

#include "page.h"
#include "platform.h"

/* Sv32 PTE bits. The bootstrap installs A/D eagerly for its identity/device
 * mappings; page-table memory remains in the mapped kernel superpage. */
enum {
  PTE_V = 1u << 0,
  PTE_R = 1u << 1,
  PTE_W = 1u << 2,
  PTE_X = 1u << 3,
  PTE_U = 1u << 4,
  PTE_A = 1u << 6,
  PTE_D = 1u << 7,
  MSTATUS_MPP = 3u << 11,
  MSTATUS_MPP_S = 1u << 11,
  SSTATUS_SPIE = 1u << 5,
  SATP_MODE_SV32 = 1u << 31,
  TASK_SLOTS = 4,
  TASK_NONE = TASK_SLOTS,
  TASK_UNUSED = 0,
  TASK_RUNNABLE = 1,
  TASK_RUNNING = 2,
  TASK_BLOCKED = 3,
  TASK_ZOMBIE = 4,
  TRAP_FRAME_BYTES = 128,
  TRAP_FRAME_A0 = 8,
  TRAP_FRAME_A7 = 15,
  TRAP_FRAME_SP = 30,
  SCAUSE_USER_ECALL = 8,
  SCAUSE_SUPERVISOR_SOFTWARE = 0x80000001u,
  SYS_FORK = 1,
  SYS_CONSOLE_PUTC = 2,
  SYS_EXIT = 3,
  SYS_WAIT = 4,
  USER_CODE_VA = 0x40000000u,
  USER_STACK_VA = USER_CODE_VA + PAGE_SIZE,
};

extern void s_entry(void);
extern void machine_timer_trap(void);
extern void user_entry(void);
extern void shell_run(void);

static volatile uint32_t supervisor_ticks;

struct task {
  uint32_t *trap_frame;
  uint32_t sepc;
  uint32_t sstatus;
  uint32_t satp;
  uint32_t *page_root;
  uint32_t *user_pt;
  uint32_t *user_stack;
  uint32_t *kernel_stack;
  uint32_t state;
  uint32_t pid;
  uint32_t parent_pid;
};

static struct task tasks[TASK_SLOTS];
static uint32_t current_task = TASK_NONE;
static volatile uint32_t scheduler_running;
static volatile uint32_t scheduled_ticks;
static uint32_t next_pid = 1;
static volatile uint32_t console_mask;
static uint32_t scheduler_free_pages;

/* Kept below the 128 KiB SoC RAM limit and aligned to their physical pages.
 * The root maps the three superpages needed by the first kernel; test_finisher
 * occupies one 4 KiB L0 mapping because it is not 4 MiB aligned. */
static volatile uint32_t root_pt[1024] __attribute__((aligned(4096)));
static volatile uint32_t low_pt[1024] __attribute__((aligned(4096)));

static uint32_t pte_leaf(uint32_t physical, uint32_t flags) {
  return ((physical >> 12) << 10) | flags;
}

static uint32_t pte_pointer(uint32_t physical) {
  return ((physical >> 12) << 10) | PTE_V;
}

static inline uint32_t csr_read_mstatus(void) {
  uint32_t value;
  __asm__ volatile("csrr %0, mstatus" : "=r"(value));
  return value;
}

static inline void csr_write_mstatus(uint32_t value) {
  __asm__ volatile("csrw mstatus, %0" :: "r"(value));
}

static inline uint32_t csr_read_sp(void) {
  uint32_t value;
  __asm__ volatile("mv %0, sp" : "=r"(value));
  return value;
}

static inline void csr_write_mepc(uint32_t value) {
  __asm__ volatile("csrw mepc, %0" :: "r"(value));
}

static inline void csr_write_satp(uint32_t value) {
  __asm__ volatile("csrw satp, %0" :: "r"(value));
}

static inline void csr_write_mtvec(uint32_t value) {
  __asm__ volatile("csrw mtvec, %0" :: "r"(value));
}

static inline void csr_write_mscratch(uint32_t value) {
  __asm__ volatile("csrw mscratch, %0" :: "r"(value));
}

static inline void csr_write_mideleg(uint32_t value) {
  __asm__ volatile("csrw mideleg, %0" :: "r"(value));
}

static inline void csr_write_medeleg(uint32_t value) {
  __asm__ volatile("csrw medeleg, %0" :: "r"(value));
}

static inline void csr_write_mie(uint32_t value) {
  __asm__ volatile("csrw mie, %0" :: "r"(value));
}

static inline uint32_t csr_read_scause(void) {
  uint32_t value;
  __asm__ volatile("csrr %0, scause" : "=r"(value));
  return value;
}

static inline uint32_t csr_read_sepc(void) {
  uint32_t value;
  __asm__ volatile("csrr %0, sepc" : "=r"(value));
  return value;
}

static inline void csr_write_sepc(uint32_t value) {
  __asm__ volatile("csrw sepc, %0" :: "r"(value));
}

static inline uint32_t csr_read_sstatus(void) {
  uint32_t value;
  __asm__ volatile("csrr %0, sstatus" : "=r"(value));
  return value;
}

static inline void csr_write_sstatus(uint32_t value) {
  __asm__ volatile("csrw sstatus, %0" :: "r"(value));
}

static inline void csr_write_sip(uint32_t value) {
  __asm__ volatile("csrw sip, %0" :: "r"(value));
}

static void clint_arm_timer(uint32_t delta) {
  volatile uint32_t *const mtimecmp =
      (volatile uint32_t *)(uintptr_t)(AX_CLINT_BASE + 0x4000u);
  volatile const uint32_t *const mtime =
      (volatile const uint32_t *)(uintptr_t)(AX_CLINT_BASE + 0xbff8u);
  mtimecmp[1] = 0xffffffffu;
  mtimecmp[0] = *mtime + delta;
  mtimecmp[1] = 0;
}

void m_setup(void) {
  const uint32_t kernel_flags = PTE_V | PTE_R | PTE_W | PTE_X | PTE_A | PTE_D;
  const uint32_t device_flags = PTE_V | PTE_R | PTE_W | PTE_A | PTE_D;

  /* Kernel RAM (VPN1 512), UART (64), and CLINT (8) are 4 MiB superpages. */
  root_pt[0x200] = pte_leaf(0x80000000u, kernel_flags);
  root_pt[0x040] = pte_leaf(AX_UART_BASE, device_flags);
  root_pt[0x008] = pte_leaf(AX_CLINT_BASE, device_flags);
  root_pt[0x000] = pte_pointer((uint32_t)(uintptr_t)low_pt);
  low_pt[(AX_TEST_BASE >> 12) & 0x3ffu] =
      pte_leaf(AX_TEST_BASE, device_flags);
  csr_write_mscratch(csr_read_sp());
  csr_write_mtvec((uint32_t)(uintptr_t)machine_timer_trap);
  csr_write_medeleg(1u << SCAUSE_USER_ECALL);
  /* MTIP stays M-owned; the shim raises delegated SSIP for S-mode policy. */
  csr_write_mideleg(1u << 1);
  csr_write_mie((1u << 7) | (1u << 1));
  /* Keep interrupts out of the bootstrap; kmain arms the first scheduler tick
   * once its allocator, task state, and trap stacks are all ready. */
  clint_arm_timer(0x00100000u);
  csr_write_satp(SATP_MODE_SV32 | ((uint32_t)(uintptr_t)root_pt >> 12));
  __asm__ volatile("sfence.vma zero, zero");
  csr_write_mepc((uint32_t)(uintptr_t)s_entry);
  csr_write_mstatus((csr_read_mstatus() & ~MSTATUS_MPP) | MSTATUS_MPP_S);
}

static uint32_t *sys_fork(uint32_t *trap_frame);
static uint32_t *sys_wait(uint32_t *trap_frame);
static uint32_t *sys_exit(uint32_t *trap_frame);

static uint32_t *schedule(uint32_t *trap_frame) {
  if (!scheduler_running) return trap_frame;

  if (current_task != TASK_NONE && tasks[current_task].state == TASK_RUNNING) {
    tasks[current_task].trap_frame = trap_frame;
    tasks[current_task].sepc = csr_read_sepc();
    tasks[current_task].sstatus = csr_read_sstatus();
    tasks[current_task].state = TASK_RUNNABLE;
  }
  const uint32_t start = current_task == TASK_NONE ? 0u :
      (current_task + 1u) % TASK_SLOTS;
  uint32_t next = TASK_NONE;
  for (uint32_t i = 0; i < TASK_SLOTS; ++i) {
    const uint32_t candidate = (start + i) % TASK_SLOTS;
    if (tasks[candidate].state == TASK_RUNNABLE) {
      next = candidate;
      break;
    }
  }
  if (next == TASK_NONE) test_finish(1);
  current_task = next;
  tasks[current_task].state = TASK_RUNNING;
  csr_write_satp(tasks[current_task].satp);
  __asm__ volatile("sfence.vma zero, zero");
  csr_write_sepc(tasks[current_task].sepc);
  csr_write_sstatus(tasks[current_task].sstatus);
  return tasks[current_task].trap_frame;
}

uint32_t *supervisor_trap(uint32_t *trap_frame) {
  const uint32_t cause = csr_read_scause();

  if (cause == SCAUSE_SUPERVISOR_SOFTWARE) {
    /* M-mode turns MTIP into delegated SSIP for scheduler policy. */
    csr_write_sip(0);
    supervisor_ticks++;
    if (!scheduler_running) return trap_frame;
    scheduled_ticks++;
    return schedule(trap_frame);
  }

  if (cause == SCAUSE_USER_ECALL) {
    const uint32_t syscall = trap_frame[TRAP_FRAME_A7];
    if (syscall == SYS_FORK) return sys_fork(trap_frame);
    if (syscall == SYS_WAIT) return sys_wait(trap_frame);
    if (syscall == SYS_EXIT) return sys_exit(trap_frame);
    if (syscall == SYS_CONSOLE_PUTC) {
      const uint32_t c = trap_frame[TRAP_FRAME_A0];
      if (c != 'P' && c != 'C' && c != 'W') test_finish(1);
      uart_putchar((char)c);
      console_mask |= c == 'P' ? 1u : c == 'C' ? 2u : 4u;
      csr_write_sepc(csr_read_sepc() + 4u);
      return trap_frame;
    }
    test_finish(1);
  }

  test_finish(1);
}

static void page_allocator_self_test(void) {
  void *pages[32];
  const uint32_t total = page_free_count();

  if (total == 0 || total > (sizeof(pages) / sizeof(pages[0]))) test_finish(1);
  for (uint32_t i = 0; i < total; ++i) {
    pages[i] = page_alloc();
    if (pages[i] == 0 || ((uintptr_t)pages[i] & (PAGE_SIZE - 1u)))
      test_finish(1);
    *(volatile uint32_t *)pages[i] = 0xa50a0000u | i;
    if (*(volatile uint32_t *)pages[i] != (0xa50a0000u | i)) test_finish(1);
  }
  if (page_alloc() != 0 || page_free_count() != 0) test_finish(1);
  while (total != page_free_count()) page_free(pages[page_free_count()]);
}

static void zero_page(uint32_t *page) {
  for (uint32_t i = 0; i < PAGE_SIZE / sizeof(*page); ++i) page[i] = 0;
}

static void copy_page(uint32_t *dst, const uint32_t *src) {
  for (uint32_t i = 0; i < PAGE_SIZE / sizeof(*dst); ++i) dst[i] = src[i];
}

static void task_release(struct task *task) {
  page_free(task->page_root);
  page_free(task->user_pt);
  page_free(task->user_stack);
  page_free(task->kernel_stack);
  task->state = TASK_UNUSED;
}

static void scheduler_make_user_task(uint32_t slot) {
  uint32_t *const page_root = page_alloc();
  uint32_t *const user_pt = page_alloc();
  uint32_t *const user_stack = page_alloc();
  uint32_t *const kernel_stack = page_alloc();
  if (page_root == 0 || user_pt == 0 || user_stack == 0 || kernel_stack == 0)
    test_finish(1);
  zero_page(page_root);
  zero_page(user_pt);
  for (uint32_t i = 0; i < 1024u; ++i) page_root[i] = root_pt[i];
  page_root[USER_CODE_VA >> 22] = pte_pointer((uint32_t)(uintptr_t)user_pt);
  user_pt[0] = pte_leaf((uint32_t)(uintptr_t)user_entry & ~(PAGE_SIZE - 1u),
                        PTE_V | PTE_R | PTE_X | PTE_U | PTE_A);
  user_pt[1] = pte_leaf((uint32_t)(uintptr_t)user_stack,
                        PTE_V | PTE_R | PTE_W | PTE_U | PTE_A | PTE_D);
  uint32_t *const frame =
      (uint32_t *)((uintptr_t)kernel_stack + PAGE_SIZE - TRAP_FRAME_BYTES);
  for (uint32_t i = 0; i < TRAP_FRAME_BYTES / sizeof(*frame); ++i) frame[i] = 0;
  tasks[slot].trap_frame = frame;
  tasks[slot].satp = SATP_MODE_SV32 | ((uint32_t)(uintptr_t)page_root >> 12);
  tasks[slot].page_root = page_root;
  tasks[slot].user_pt = user_pt;
  tasks[slot].user_stack = user_stack;
  tasks[slot].kernel_stack = kernel_stack;
  tasks[slot].state = TASK_RUNNABLE;
  tasks[slot].pid = next_pid++;
  tasks[slot].parent_pid = 0;
  tasks[slot].sepc = USER_CODE_VA +
      ((uint32_t)(uintptr_t)user_entry & (PAGE_SIZE - 1u));
  frame[TRAP_FRAME_A0] = slot;
  frame[TRAP_FRAME_SP] = USER_STACK_VA + PAGE_SIZE;
  /* SRET returns to U mode with supervisor interrupts enabled. */
  tasks[slot].sstatus = SSTATUS_SPIE;
}

static uint32_t *sys_fork(uint32_t *trap_frame) {
  if (current_task == TASK_NONE || tasks[current_task].state != TASK_RUNNING)
    test_finish(1);
  uint32_t slot = TASK_NONE;
  for (uint32_t i = 0; i < TASK_SLOTS; ++i)
    if (tasks[i].state == TASK_UNUSED) { slot = i; break; }
  if (slot == TASK_NONE) test_finish(1);

  struct task *const parent = &tasks[current_task];
  struct task *const child = &tasks[slot];
  uint32_t *const page_root = page_alloc();
  uint32_t *const user_pt = page_alloc();
  uint32_t *const user_stack = page_alloc();
  uint32_t *const kernel_stack = page_alloc();
  if (page_root == 0 || user_pt == 0 || user_stack == 0 || kernel_stack == 0)
    test_finish(1);
  copy_page(page_root, parent->page_root);
  copy_page(user_pt, parent->user_pt);
  copy_page(user_stack, parent->user_stack);
  user_pt[1] = pte_leaf((uint32_t)(uintptr_t)user_stack,
                        PTE_V | PTE_R | PTE_W | PTE_U | PTE_A | PTE_D);
  page_root[USER_CODE_VA >> 22] = pte_pointer((uint32_t)(uintptr_t)user_pt);
  uint32_t *const frame =
      (uint32_t *)((uintptr_t)kernel_stack + PAGE_SIZE - TRAP_FRAME_BYTES);
  for (uint32_t i = 0; i < TRAP_FRAME_BYTES / sizeof(*frame); ++i)
    frame[i] = trap_frame[i];
  if (user_stack[0] != 0x51a00001u) test_finish(1);
  child->trap_frame = frame;
  child->sepc = csr_read_sepc() + 4u;
  child->sstatus = csr_read_sstatus();
  child->satp = SATP_MODE_SV32 | ((uint32_t)(uintptr_t)page_root >> 12);
  child->page_root = page_root;
  child->user_pt = user_pt;
  child->user_stack = user_stack;
  child->kernel_stack = kernel_stack;
  child->state = TASK_RUNNABLE;
  child->pid = next_pid++;
  child->parent_pid = parent->pid;
  frame[TRAP_FRAME_A0] = 0;
  trap_frame[TRAP_FRAME_A0] = child->pid;
  csr_write_sepc(child->sepc);
  return trap_frame;
}

static uint32_t *sys_wait(uint32_t *trap_frame) {
  if (current_task == TASK_NONE) test_finish(1);
  struct task *const parent = &tasks[current_task];
  for (uint32_t i = 0; i < TASK_SLOTS; ++i) {
    struct task *const child = &tasks[i];
    if (child->parent_pid != parent->pid) continue;
    if (child->state == TASK_ZOMBIE) {
      trap_frame[TRAP_FRAME_A0] = child->pid;
      task_release(child);
      csr_write_sepc(csr_read_sepc() + 4u);
      return trap_frame;
    }
    parent->trap_frame = trap_frame;
    parent->sepc = csr_read_sepc() + 4u;
    parent->sstatus = csr_read_sstatus();
    parent->state = TASK_BLOCKED;
    return schedule(trap_frame);
  }
  test_finish(1);
}

static uint32_t *sys_exit(uint32_t *trap_frame) {
  if (current_task == TASK_NONE || trap_frame[TRAP_FRAME_A0] != 0u)
    test_finish(1);
  struct task *const task = &tasks[current_task];
  task->state = TASK_ZOMBIE;
  for (uint32_t i = 0; i < TASK_SLOTS; ++i) {
    struct task *const parent = &tasks[i];
    if (parent->state == TASK_BLOCKED && parent->pid == task->parent_pid) {
      parent->trap_frame[TRAP_FRAME_A0] = task->pid;
      parent->state = TASK_RUNNABLE;
      task_release(task);
      return schedule(trap_frame);
    }
  }
  if (task->parent_pid != 0) return schedule(trap_frame);
  csr_write_satp(SATP_MODE_SV32 | ((uint32_t)(uintptr_t)root_pt >> 12));
  __asm__ volatile("sfence.vma zero, zero");
  task_release(task);
  if (console_mask != 7u || page_free_count() != scheduler_free_pages) test_finish(1);
  test_finish(0);
}

static void scheduler_start(void) {
  scheduler_free_pages = page_free_count();
  scheduler_make_user_task(0);
  scheduler_running = 1;
  clint_arm_timer(2000);
}

void kernel_fork_demo(void) {
  scheduler_start();
  for (;;) __asm__ volatile("wfi");
}

void kmain(void) {
  page_init();
  page_allocator_self_test();
  shell_run();
}
