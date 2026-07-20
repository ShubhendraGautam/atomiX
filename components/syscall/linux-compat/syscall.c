/* syscall.linux-compat: the RISC-V Linux syscall subset (docs/abi.md).
 *
 * Numbers come from asm-generic/unistd.h, which is the table RISC-V Linux uses.
 * They are not ours to choose, and that is the point: a standard table means an
 * unmodified newlib or picolibc can be retargeted onto this kernel, and a
 * program built for it is not built for atomiX alone.
 *
 * Errors return -errno, so a libc wrapper is the ordinary "negate into errno,
 * return -1" shim rather than something bespoke.
 *
 * Calls with no Linux equivalent live at 0x1000 and above, a range the
 * asm-generic table will never reach.  The accelerator role driver goes there
 * rather than being disguised as an ioctl -- it is the most interesting thing
 * this machine does and it deserves a name.
 *
 * What this component does NOT decide: how a task is forked, how pages are
 * allocated, or how the console is driven.  Those arrive through
 * `struct syscall_ops`, so replacing the ABI does not mean reimplementing the
 * kernel.
 *
 * Sizes are build-time parameters (see component.json), because a descriptor
 * table large enough for a shell and one small enough for a 128 KiB image are
 * the same design at different settings. */
#include <stdint.h>

#include "syscall.h"

#ifndef AXOS_MAX_FDS
#define AXOS_MAX_FDS 8
#endif
#ifndef AXOS_WRITE_MAX
#define AXOS_WRITE_MAX 1024
#endif

/* asm-generic/unistd.h numbers.  Only the ones this kernel answers are named;
 * everything else falls through to -ENOSYS, which a libc handles correctly. */
enum {
  NR_openat = 56,
  NR_close = 57,
  NR_lseek = 62,
  NR_read = 63,
  NR_write = 64,
  NR_fstat = 80,
  NR_exit = 93,
  NR_getpid = 172,
  NR_brk = 214,
  NR_clone = 220,
  NR_wait4 = 260,

  /* Private range: ours to define and to change. */
  NR_ax_role_info = 0x1000,
  NR_ax_role_submit = 0x1001,
};

/* Standard descriptor numbers.  Only the console exists so far; a real
 * descriptor table arrives with the filesystem binding. */
enum { FD_STDIN = 0, FD_STDOUT = 1, FD_STDERR = 2 };

static inline uint32_t csr_read_sepc(void) {
  uint32_t value;
  __asm__ volatile("csrr %0, sepc" : "=r"(value));
  return value;
}
static inline void csr_write_sepc(uint32_t value) {
  __asm__ volatile("csrw sepc, %0" :: "r"(value));
}

/* Return `value` to the caller and resume after the ecall.  Every call that
 * returns to its caller ends through here, so sepc advancement happens in
 * exactly one place. */
static uint32_t *ret(uint32_t *trap_frame, uint32_t value) {
  trap_frame[TRAP_FRAME_A0] = value;
  csr_write_sepc(csr_read_sepc() + 4u);
  return trap_frame;
}

/* Errors are negative: -errno as a two's-complement word. */
static uint32_t *err(uint32_t *trap_frame, uint32_t errno_value) {
  return ret(trap_frame, (uint32_t)(-(int32_t)errno_value));
}

/* write(fd, buf, count).
 *
 * Only the console descriptors are writable so far.  The copy is bounded and
 * goes through the kernel's user-copy hook, so a bad pointer becomes -EFAULT
 * rather than a supervisor fault -- a user program must not be able to panic
 * the kernel by passing a garbage address. */
static uint32_t *sys_write(uint32_t *trap_frame, const struct syscall_ops *ops) {
  const uint32_t fd = trap_frame[TRAP_FRAME_A0];
  const uint32_t buf = trap_frame[TRAP_FRAME_A1];
  const uint32_t count = trap_frame[TRAP_FRAME_A2];

  if (fd != FD_STDOUT && fd != FD_STDERR) return err(trap_frame, AX_EBADF);
  if (count == 0) return ret(trap_frame, 0);

  const uint32_t chunk = count > AXOS_WRITE_MAX ? AXOS_WRITE_MAX : count;
  for (uint32_t i = 0; i < chunk; ++i) {
    char c;
    if (!ops->copy_from_user(&c, buf + i, 1)) {
      /* Partial writes are legal and are what Linux does: report what landed. */
      return i ? ret(trap_frame, i) : err(trap_frame, AX_EFAULT);
    }
    ops->console_putc(c);
  }
  return ret(trap_frame, chunk);
}

/* read(fd, buf, count).  No input source is bound yet; end-of-file (0) is the
 * honest answer for the console and keeps a libc's getchar loop terminating
 * instead of spinning. */
static uint32_t *sys_read(uint32_t *trap_frame) {
  const uint32_t fd = trap_frame[TRAP_FRAME_A0];
  if (fd != FD_STDIN) return err(trap_frame, AX_EBADF);
  return ret(trap_frame, 0);
}

uint32_t *syscall_dispatch(uint32_t *trap_frame, const struct syscall_ops *ops) {
  const uint32_t nr = trap_frame[TRAP_FRAME_A7];

  switch (nr) {
    /* --- process control -------------------------------------------------
     * These delegate to the kernel and may resume a different task, so they
     * do not go through ret(): the hook owns sepc for the frame it returns. */
    case NR_clone:
      /* RISC-V has no fork.  clone with a zero stack pointer and no thread
       * flags is fork; anything else needs a thread model this kernel does not
       * have, so it is refused rather than silently mishandled. */
      if (trap_frame[TRAP_FRAME_A1] != 0) return err(trap_frame, AX_EINVAL);
      return ops->fork(trap_frame);

    case NR_wait4:
      /* wait4(pid, status, options, rusage); only the blocking any-child form
       * exists.  rusage is accepted and ignored, as a null pointer would be. */
      return ops->wait(trap_frame, trap_frame[TRAP_FRAME_A1]);

    case NR_exit:
      return ops->exit(trap_frame, trap_frame[TRAP_FRAME_A0]);

    /* --- simple returns --------------------------------------------------- */
    case NR_getpid:
      return ret(trap_frame, ops->getpid());

    case NR_brk: {
      const uint32_t requested = trap_frame[TRAP_FRAME_A0];
      const uint32_t result = ops->brk(requested);
      /* brk reports failure by returning the unchanged break, not by an errno:
       * that is the kernel-level contract a libc's sbrk is written against. */
      return ret(trap_frame, result);
    }

    case NR_write:
      return sys_write(trap_frame, ops);

    case NR_read:
      return sys_read(trap_frame);

    /* --- declared but not yet backed --------------------------------------
     * These need the descriptor table and the filesystem binding.  They are
     * named here so the boundary is visible: -ENOSYS is a correct answer that
     * a libc understands, and an unnamed number returning -ENOSYS by accident
     * would not tell a reader that the work is known and scheduled. */
    case NR_openat:
    case NR_close:
    case NR_lseek:
    case NR_fstat:
      return err(trap_frame, AX_ENOSYS);

    /* --- atomiX private range ---------------------------------------------
     * The role driver lands here in the next stage; the numbers are reserved
     * now so the ABI does not shift under software written against it. */
    case NR_ax_role_info:
    case NR_ax_role_submit:
      return err(trap_frame, AX_ENOSYS);

    default:
      /* An unknown number is never fatal.  A libc probing for a call it can
       * live without must get -ENOSYS back, not a dead process. */
      return err(trap_frame, AX_ENOSYS);
  }
}
