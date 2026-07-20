/* axlibc: console output.
 *
 * A line-buffered writer plus the printf conversions a program actually needs
 * on this machine: %d, %u, %x, %s, %c, %p, %%, with a width and zero-pad flag.
 *
 * No floating point, on purpose: this target has no FPU, a soft-float %f pulls
 * in a large chunk of libgcc, and nothing here needs it.  An unsupported
 * conversion prints itself verbatim rather than being silently dropped, so a
 * format string mistake is visible instead of producing quietly wrong output.
 *
 * Buffering exists because every write() is an ecall and a context switch;
 * printing a line a character at a time would make output cost more than the
 * work producing it. */
#include "axlibc.h"

#include <stdarg.h>

#define BUFSZ 128
static char outbuf[BUFSZ];
static size_t outlen;

static void flush(void) {
  if (outlen == 0) return;
  write(1, outbuf, outlen);
  outlen = 0;
}

static void emit(char c) {
  outbuf[outlen++] = c;
  /* Flush on a full buffer or a completed line: a program that exits without
   * a trailing newline still gets its output, because exit flushes too. */
  if (outlen == BUFSZ || c == '\n') flush();
}

int putchar(int c) { emit((char)c); return c; }

int puts(const char *s) {
  while (*s) emit(*s++);
  emit('\n');
  return 0;
}

static void emit_str(const char *s, int width, int zero) {
  int len = 0;
  while (s[len]) ++len;
  for (int i = len; i < width; ++i) emit(zero ? '0' : ' ');
  for (int i = 0; i < len; ++i) emit(s[i]);
}

/* Render into a caller-supplied buffer, most significant digit first. */
static void utoa(uint32_t v, unsigned base, char *out, int upper) {
  const char *digits = upper ? "0123456789ABCDEF" : "0123456789abcdef";
  char tmp[12];
  int n = 0;
  if (v == 0) tmp[n++] = '0';
  while (v) { tmp[n++] = digits[v % base]; v /= base; }
  int i = 0;
  while (n) out[i++] = tmp[--n];
  out[i] = '\0';
}

int printf(const char *fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  for (const char *p = fmt; *p; ++p) {
    if (*p != '%') { emit(*p); continue; }
    ++p;
    if (*p == '\0') { emit('%'); break; }

    int zero = 0, width = 0;
    if (*p == '0') { zero = 1; ++p; }
    while (*p >= '0' && *p <= '9') { width = width * 10 + (*p - '0'); ++p; }

    char num[16];
    switch (*p) {
      case 'd': {
        int v = va_arg(ap, int);
        if (v < 0) {
          /* Negate as unsigned so INT_MIN does not overflow on the way in. */
          utoa((uint32_t)(-(int64_t)v), 10, num + 1, 0);
          num[0] = '-';
          emit_str(num, width, 0);
        } else {
          utoa((uint32_t)v, 10, num, 0);
          emit_str(num, width, zero);
        }
        break;
      }
      case 'u': utoa(va_arg(ap, uint32_t), 10, num, 0); emit_str(num, width, zero); break;
      case 'x': utoa(va_arg(ap, uint32_t), 16, num, 0); emit_str(num, width, zero); break;
      case 'X': utoa(va_arg(ap, uint32_t), 16, num, 1); emit_str(num, width, zero); break;
      case 'p': emit('0'); emit('x');
                utoa((uint32_t)(uintptr_t)va_arg(ap, void *), 16, num, 0);
                emit_str(num, 8, 1); break;
      case 'c': emit((char)va_arg(ap, int)); break;
      case 's': { const char *s = va_arg(ap, const char *);
                  emit_str(s ? s : "(null)", width, 0); break; }
      case '%': emit('%'); break;
      /* Unknown conversion: show it rather than swallowing it. */
      default: emit('%'); emit(*p); break;
    }
  }
  va_end(ap);
  flush();
  return 0;
}
