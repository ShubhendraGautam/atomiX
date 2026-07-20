#include <stdint.h>

#include "platform.h"
#include "fs.h"
#include "role.h"

extern void kernel_fork_demo(void);
extern void kernel_exec_demo(void);

struct ram_file {
  const char *name;
  const char *data;
};

/* The first filesystem is intentionally immutable and memory-resident. Its
 * interface is file-like from day one, so replacing this table with an inode
 * layer later does not change the shell contract. */
static const struct ram_file ramdisk[] = {
    {"motd", "Welcome to aXos.\n"},
    {"readme", "aXos RAM disk: help, ls, cat, echo, exit.\n"},
};
static int disk_mounted;

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

static void shell_ls(void) {
  if (disk_mounted) { fs_list(); return; }
  for (uint32_t i = 0; i < sizeof(ramdisk) / sizeof(ramdisk[0]); ++i) {
    uart_puts(ramdisk[i].name);
    uart_puts("\n");
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

static void shell_cat(const char *name) {
  if (disk_mounted) {
    if (fs_cat(name)) uart_puts("cat: no such file\n");
    return;
  }
  for (uint32_t i = 0; i < sizeof(ramdisk) / sizeof(ramdisk[0]); ++i) {
    if (streq(name, ramdisk[i].name)) {
      uart_puts(ramdisk[i].data);
      return;
    }
  }
  uart_puts("cat: no such file\n");
}

void shell_run(void) {
  char line[80];
  disk_mounted = fs_mount() == 0;
  uart_puts("aXos: shell online\n");
  for (;;) {
    uart_puts("aXos> ");
    readline(line, sizeof(line));
    if (streq(line, "")) continue;
    if (streq(line, "help")) {
      uart_puts("commands: help ls cat write echo fork exec role exit\n");
    } else if (streq(line, "ls")) {
      shell_ls();
    } else if (starts_with(line, "cat ")) {
      shell_cat(line + 4);
    } else if (starts_with(line, "write ")) {
      char *name = line + 6;
      char *data = name;
      while (*data && *data != ' ') ++data;
      if (!*data) {
        uart_puts("write: usage write NAME TEXT\n");
      } else if (!disk_mounted) {
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
