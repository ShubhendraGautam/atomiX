/* The first ordinary C program aXos runs.
 *
 * It is compiled and linked entirely separately from the kernel and reaches it
 * only as an opaque byte array, so nothing here is resolved at kernel link
 * time.  What it demonstrates, beyond the loader, is that a program can now be
 * written *normally*: a main() with a return value, malloc, printf, string
 * functions, and 64-bit arithmetic, none of which existed a stage ago.
 *
 * Each check exits with a distinct code so a failure says which part broke
 * rather than merely that something did. */
#include "axlibc.h"

/* .rodata, .data and .bss, so the loader's per-segment mapping is still
 * exercised now that a library sits on top of it. */
static const char greeting[] = "axlibc";
static volatile int data_marker = 0x5a;
static volatile int bss_marker;

int main(void) {
  if (data_marker != 0x5a) return 2;
  if (bss_marker != 0) return 3;
  data_marker = 0x11;
  bss_marker = 0x22;
  if (data_marker != 0x11 || bss_marker != 0x22) return 4;

  /* strings */
  if (strlen(greeting) != 6) return 5;
  if (strcmp(greeting, "axlibc") != 0) return 6;

  /* malloc: allocate, write a pattern, read it back.  The heap has to be
   * mapped by brk() for this to work at all, so a failure here is as likely to
   * be the kernel's brk as the allocator. */
  char *const buf = malloc(200);
  if (buf == NULL) return 7;
  memset(buf, 'x', 200);
  buf[199] = '\0';
  if (strlen(buf) != 199) return 8;

  /* Free and reallocate: exercises the free list rather than only the bump
   * path, and would catch a coalesce that corrupts the chain. */
  free(buf);
  char *const again = malloc(64);
  if (again == NULL) return 9;
  strcpy(again, "reused");
  if (strcmp(again, "reused") != 0) return 10;

  /* calloc must zero. */
  int *const zeros = calloc(16, sizeof(int));
  if (zeros == NULL) return 11;
  for (int i = 0; i < 16; ++i)
    if (zeros[i] != 0) return 12;

  /* A large allocation forces sbrk to grow the heap across a page boundary,
   * which is the path that maps new pages. */
  char *const big = malloc(6000);
  if (big == NULL) return 13;
  big[0] = 'a';
  big[5999] = 'z';
  if (big[0] != 'a' || big[5999] != 'z') return 14;

  /* 64-bit arithmetic: this is what the render benchmark tripped over as an
   * undefined __udivdi3 when nothing supplied libgcc. */
  const uint64_t wide = 1000000007ull * 3ull;
  if (wide / 3ull != 1000000007ull) return 15;

  printf("%s: pid=%d n=%d hex=%x str=%s\n", greeting, getpid() > 0 ? 1 : 0,
         42, 0xbeef, again);
  return 0;
}
