#include <stdint.h>

#include "platform.h"

extern void kernel_fork_demo(void);

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
  for (uint32_t i = 0; i < sizeof(ramdisk) / sizeof(ramdisk[0]); ++i) {
    uart_puts(ramdisk[i].name);
    uart_puts("\n");
  }
}

static void shell_cat(const char *name) {
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
  uart_puts("aXos: shell online\n");
  for (;;) {
    uart_puts("aXos> ");
    readline(line, sizeof(line));
    if (streq(line, "")) continue;
    if (streq(line, "help")) {
      uart_puts("commands: help ls cat echo fork exit\n");
    } else if (streq(line, "ls")) {
      shell_ls();
    } else if (starts_with(line, "cat ")) {
      shell_cat(line + 4);
    } else if (starts_with(line, "echo ")) {
      uart_puts(line + 5);
      uart_puts("\n");
    } else if (streq(line, "fork")) {
      uart_puts("fork demo: ");
      kernel_fork_demo();
    } else if (streq(line, "exit")) {
      test_finish(0);
    } else {
      uart_puts("sh: command not found\n");
    }
  }
}
