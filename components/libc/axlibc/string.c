/* axlibc: string and memory primitives.
 *
 * Straightforward byte loops.  They are compiled with -ffreestanding, but GCC
 * still emits calls to memcpy/memset for struct assignment and array
 * initialisation, so these must exist regardless of whether a program calls
 * them by name. */
#include "axlibc.h"

void *memcpy(void *dst, const void *src, size_t n) {
  uint8_t *d = dst;
  const uint8_t *s = src;
  for (size_t i = 0; i < n; ++i) d[i] = s[i];
  return dst;
}

void *memmove(void *dst, const void *src, size_t n) {
  uint8_t *d = dst;
  const uint8_t *s = src;
  /* Overlap matters only when the destination is inside the source range and
   * above it; copying backwards then reads each byte before it is overwritten. */
  if (d > s && d < s + n)
    for (size_t i = n; i-- > 0;) d[i] = s[i];
  else
    for (size_t i = 0; i < n; ++i) d[i] = s[i];
  return dst;
}

void *memset(void *dst, int c, size_t n) {
  uint8_t *d = dst;
  for (size_t i = 0; i < n; ++i) d[i] = (uint8_t)c;
  return dst;
}

int memcmp(const void *a, const void *b, size_t n) {
  const uint8_t *x = a, *y = b;
  for (size_t i = 0; i < n; ++i)
    if (x[i] != y[i]) return (int)x[i] - (int)y[i];
  return 0;
}

size_t strlen(const char *s) {
  size_t n = 0;
  while (s[n]) ++n;
  return n;
}

int strcmp(const char *a, const char *b) {
  while (*a && *a == *b) { ++a; ++b; }
  return (int)(uint8_t)*a - (int)(uint8_t)*b;
}

int strncmp(const char *a, const char *b, size_t n) {
  for (size_t i = 0; i < n; ++i) {
    if (a[i] != b[i]) return (int)(uint8_t)a[i] - (int)(uint8_t)b[i];
    if (!a[i]) break;
  }
  return 0;
}

char *strcpy(char *dst, const char *src) {
  size_t i = 0;
  for (; src[i]; ++i) dst[i] = src[i];
  dst[i] = '\0';
  return dst;
}

char *strchr(const char *s, int c) {
  for (;; ++s) {
    if (*s == (char)c) return (char *)s;
    if (!*s) return NULL;
  }
}
