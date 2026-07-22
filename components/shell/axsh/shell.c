#include <stdint.h>

#include "platform.h"
#include "fs.h"
#include "role.h"

extern void kernel_fork_demo(void);
extern void kernel_exec_demo(void);

/* What fs_mount() reported: FS_MOUNT_RW, FS_MOUNT_RO, or negative.  The shell
 * keeps no files of its own -- a diskless boot gets the filesystem component's
 * built-in root, so `ls` and `cat` have exactly one implementation behind them
 * whether or not a card is present. */
static int mount_state;

static int streq(const char *a, const char *b) {
  while (*a && *a == *b) { ++a; ++b; }
  return *a == *b;
}

static int starts_with(const char *text, const char *prefix) {
  while (*prefix) {
    if (*text++ != *prefix++) return 0;
  }
  return 1;
}

static void readline(char *line, uint32_t capacity) {
  uint32_t length = 0;
  for (;;) {
    const char c = uart_getchar();
    if (c == '\r' || c == '\n') {
      uart_puts("\n");
      line[length] = 0;
      return;
    }
    if ((c == '\b' || c == 0x7f) && length) {
      --length;
      uart_puts("\b \b");
    } else if (c >= ' ' && c <= '~' && length + 1 < capacity) {
      line[length++] = c;
      uart_putchar(c);
    }
  }
}


/* Discover the accelerator role through the shell control plane and, when the
 * loopback contract-proof role is present, drive one job end-to-end. */
static void shell_role(void) {
  const uint32_t id = role_discover();
  if (id == 0) {
    uart_puts("role: none\n");
    return;
  }
  uart_puts("role: ");
  uart_puts(role_name(id));
  /* VERSION is a single-digit programming-model revision (1 today). */
  uart_puts(" v");
  uart_putchar((char)('0' + (role_version() & 0xfu)));
  uart_puts("\n");
  if (id == AX_ROLE_ID_LOOPBACK)
    uart_puts(role_loopback_selftest() == 0 ? "role: copy ok\n"
                                            : "role: copy FAIL\n");
}

/* `cat` is a loop over the same fs_read the read() syscall uses, rather than a
 * "print this file" call the filesystem provides for the shell alone.  One read
 * path means the shell test and the ABI test cover the same code. */
static void shell_cat(const char *name) {
  const int file = fs_lookup(name);
  if (file < 0) {
    uart_puts("cat: no such file\n");
    return;
  }
  char chunk[64];
  for (uint32_t offset = 0;;) {
    const int32_t got = fs_read(file, offset, chunk, sizeof(chunk));
    if (got <= 0) {
      if (got < 0) uart_puts("cat: read error\n");
      return;
    }
    for (int32_t i = 0; i < got; ++i) uart_putchar(chunk[i]);
    offset += (uint32_t)got;
  }
}

void shell_run(void) {
  char line[80];
  mount_state = fs_mount();
  uart_puts("aXos: shell online\n");
  for (;;) {
    uart_puts("aXos> ");
    readline(line, sizeof(line));
    if (streq(line, "")) continue;
    if (streq(line, "help")) {
      uart_puts("commands: help ls cat write echo fork exec role exit\n");
    } else if (streq(line, "ls")) {
      fs_list();
    } else if (starts_with(line, "cat ")) {
      shell_cat(line + 4);
    } else if (starts_with(line, "write ")) {
      char *name = line + 6;
      char *data = name;
      while (*data && *data != ' ') ++data;
      if (!*data) {
        uart_puts("write: usage write NAME TEXT\n");
      } else if (mount_state != FS_MOUNT_RW) {
        uart_puts("write: no writable disk\n");
      } else {
        *data++ = 0;
        if (fs_write(name, data)) uart_puts("write: failed\n");
      }
    } else if (starts_with(line, "echo ")) {
      uart_puts(line + 5);
      uart_puts("\n");
    } else if (streq(line, "fork")) {
      uart_puts("fork demo: ");
      kernel_fork_demo();
    } else if (streq(line, "exec")) {
      /* Load and run the embedded ELF.  Nothing about the program is known at
       * kernel link time, so its output is the loader's evidence. */
      uart_puts("exec: ");
      kernel_exec_demo();
    } else if (streq(line, "role")) {
      shell_role();
    } else if (streq(line, "exit")) {
      test_finish(0);
    } else {
      uart_puts("sh: command not found\n");
    }
  }
}
