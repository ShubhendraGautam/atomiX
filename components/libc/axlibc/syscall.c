/* axlibc: the syscall layer.
 *
 * One inline `ecall` wrapper per argument count, plus the errno convention from
 * docs/abi.md: a return in [-4095, -1] is -errno, anything else is a result.
 * That is the whole reason to follow a standard ABI -- this file is the
 * ordinary three-line shim rather than something bespoke.
 *
 * Numbers are the asm-generic ones; they are not repeated as an enum here so
 * there is exactly one place (the kernel component) that defines them and one
 * place (docs/abi.md) that documents them. */
#include "axlibc.h"

int errno;
char **__libc_environ;

/* Turn a raw kernel return into the (result, errno) pair C expects. */
long __libc_ret(long value) {
  if (value < 0 && value >= -4095) {
    errno = (int)-value;
    return -1;
  }
  return value;
}

long __libc_syscall(long nr, long a0, long a1, long a2) {
  register long r_a7 __asm__("a7") = nr;
  register long r_a0 __asm__("a0") = a0;
  register long r_a1 __asm__("a1") = a1;
  register long r_a2 __asm__("a2") = a2;
  __asm__ volatile("ecall"
                   : "+r"(r_a0)
                   : "r"(r_a7), "r"(r_a1), "r"(r_a2)
                   : "memory");
  return r_a0;
}

ssize_t write(int fd, const void *buf, size_t count) {
  return __libc_ret(__libc_syscall(64, fd, (long)buf, (long)count));
}

ssize_t read(int fd, void *buf, size_t count) {
  return __libc_ret(__libc_syscall(63, fd, (long)buf, (long)count));
}

int getpid(void) { return (int)__libc_ret(__libc_syscall(172, 0, 0, 0)); }

void _exit(int status) {
  __libc_syscall(93, status, 0, 0);
  for (;;) {}                     /* exit does not return */
}

void exit(int status) { _exit(status); }

/* sbrk over the kernel's brk.  The kernel reports failure by returning the
 * break unchanged, never by an errno, so that is what is checked here. */
void *sbrk(intptr_t increment) {
  const long current = __libc_syscall(214, 0, 0, 0);
  if (current <= 0) { errno = ENOMEM; return (void *)-1; }
  if (increment == 0) return (void *)current;

  const long wanted = current + increment;
  const long got = __libc_syscall(214, wanted, 0, 0);
  if (got != wanted) { errno = ENOMEM; return (void *)-1; }
  return (void *)current;
}
