/* Render-workload benchmark: the shape of a 1993-era software renderer.
 *
 * cpu_perf.c measures the core on a small working set that lives entirely in
 * cache, which flatters any machine with a good front end.  This benchmark
 * exists because that number does not predict whether a real program -- a
 * software renderer being the canonical hard case -- will run well.  It stresses
 * the three things such a program actually depends on, none of which cpu_perf
 * touches:
 *
 *   column   texture-mapped vertical column fill.  Scattered byte reads out of a
 *            texture set far larger than the data cache, plus a strided write
 *            into a framebuffer.  This is the classic R_DrawColumn shape and it
 *            is almost purely a memory-system measurement.
 *   span     horizontal span fill with two-dimensional texture coordinates.
 *            Worse locality again: the texture walk moves in both u and v, so
 *            consecutive pixels touch different cache lines.
 *   fixdiv   the fixed-point divide that a perspective renderer performs per
 *            column.  Pure divider-latency measurement.
 *   blit     a straight framebuffer copy -- sequential bandwidth, the best case
 *            for a line-based cache.
 *
 * Results are reported in cycles per pixel (or per divide), which is the figure
 * that composes: at a 25 MHz part, 320x200 at 35 fps needs the whole frame in
 * about 3570 cycles per scanline, so cycles-per-pixel is directly comparable
 * against a frame-rate target.
 *
 * The working set is ~52 KiB: comfortably larger than any cache these profiles
 * configure, and still inside the 128 KiB the bare-metal link script allows.
 * That ceiling is itself worth noting -- a real renderer needs megabytes, and
 * raising it is part of the userspace work, not of this benchmark. */
#include "platform.h"

#define FB_W    320
#define FB_H    64
#define TEX_N   8           /* textures, chosen round-robin to defeat caching */
#define TEX_W   64
#define TEX_H   64
#define TEX_SZ  (TEX_W * TEX_H)

static uint8_t fb[FB_W * FB_H];             /* 20480 B */
static uint8_t tex[TEX_N][TEX_SZ];          /* 32768 B */
static uint8_t colormap[256];

static uint32_t rdcycle(void) {
  uint32_t v; __asm__ volatile("csrr %0, mcycle" : "=r"(v)); return v;
}

static void putdec(uint32_t v) {
  char buf[12]; int n = 0;
  if (!v) { uart_putchar('0'); return; }
  while (v) { buf[n++] = (char)('0' + v % 10); v /= 10; }
  while (n) uart_putchar(buf[--n]);
}

/* Cycles per unit, in hundredths, without floating point. */
static void report(const char *name, uint32_t cycles, uint32_t units) {
  /* 32-bit only: the bare-metal link has no libgcc, so a 64-bit divide would
     be an undefined __udivdi3.  Scale before multiplying when the product
     would overflow instead. */
  uint32_t cp100;
  if (!units)                        cp100 = 0;
  else if (cycles < 42949672u)       cp100 = (cycles * 100u) / units;
  else                               cp100 = (cycles / units) * 100u;
  uart_puts("  ");
  uart_puts(name);
  uart_puts(": cycles=");
  putdec(cycles);
  uart_puts(" units=");
  putdec(units);
  uart_puts(" cyc_per_unit_x100=");
  putdec(cp100);
  uart_puts("\n");
}

static uint32_t rng = 0x13579bdfu;
static uint32_t rnd(void) { rng = rng * 1664525u + 1013904223u; return rng; }

static void init_data(void) {
  for (int i = 0; i < 256; ++i) colormap[i] = (uint8_t)(255 - i);
  for (int t = 0; t < TEX_N; ++t)
    for (int i = 0; i < TEX_SZ; ++i)
      tex[t][i] = (uint8_t)(rnd() >> 13);
  for (int i = 0; i < FB_W * FB_H; ++i) fb[i] = 0;
}

/* R_DrawColumn: walk a texture with a 16.16 fixed-point step, writing one
 * framebuffer column.  Reads scatter through the texture; writes stride by the
 * framebuffer width, so neither side is sequential. */
static uint32_t bench_column(void) {
  const int ncols = FB_W * 4;
  uint32_t pixels = 0;
  uint32_t c0 = rdcycle();
  for (int c = 0; c < ncols; ++c) {
    const uint8_t *src = tex[c & (TEX_N - 1)];
    uint8_t *dest = &fb[c % FB_W];
    uint32_t frac = (uint32_t)(c * 7919);
    uint32_t step = 0x00010000u + (uint32_t)(c << 6);
    for (int y = 0; y < FB_H; ++y) {
      dest[y * FB_W] = colormap[src[(frac >> 16) & (TEX_SZ - 1)]];
      frac += step;
    }
    pixels += FB_H;
  }
  uint32_t cyc = rdcycle() - c0;
  report("column", cyc, pixels);
  return cyc;
}

/* R_DrawSpan: horizontal fill, texture coordinates stepping in both axes, so
 * successive reads land on different lines. */
static uint32_t bench_span(void) {
  const int nspans = FB_H * 8;
  uint32_t pixels = 0;
  uint32_t c0 = rdcycle();
  for (int s = 0; s < nspans; ++s) {
    const uint8_t *src = tex[s & (TEX_N - 1)];
    uint8_t *dest = &fb[(s % FB_H) * FB_W];
    uint32_t u = (uint32_t)(s * 4409), v = (uint32_t)(s * 7919);
    uint32_t du = 0x00013000u, dv = 0x00027000u;
    for (int x = 0; x < FB_W; ++x) {
      uint32_t tu = (u >> 16) & (TEX_W - 1);
      uint32_t tv = (v >> 16) & (TEX_H - 1);
      dest[x] = src[tv * TEX_W + tu];
      u += du; v += dv;
    }
    pixels += FB_W;
  }
  uint32_t cyc = rdcycle() - c0;
  report("span", cyc, pixels);
  return cyc;
}

/* The perspective divide a renderer does per column: scale = projection/dist.
 * One RV32M DIV per iteration, so this isolates divider latency. */
static uint32_t bench_fixdiv(void) {
  const int n = 4000;
  volatile int32_t sink = 0;
  int32_t acc = 0;
  uint32_t c0 = rdcycle();
  for (int i = 0; i < n; ++i) {
    int32_t dist = (int32_t)((i & 1023) + 1);
    int32_t scale = (int32_t)(0x40000000 / dist);
    acc += scale >> 8;
  }
  uint32_t cyc = rdcycle() - c0;
  sink = acc; (void)sink;
  report("fixdiv", cyc, (uint32_t)n);
  return cyc;
}

/* Sequential framebuffer copy: pure streaming bandwidth. */
static uint32_t bench_blit(void) {
  uint32_t c0 = rdcycle();
  for (int r = 0; r < 4; ++r)
    for (int i = 0; i < FB_W * FB_H; ++i) fb[i] = (uint8_t)(fb[i] + 1);
  uint32_t cyc = rdcycle() - c0;
  report("blit", cyc, (uint32_t)(FB_W * FB_H * 4));
  return cyc;
}

int main(void) {
  uart_puts("render_perf:\n");
  init_data();

  uint32_t total = 0;
  total += bench_column();
  total += bench_span();
  total += bench_fixdiv();
  total += bench_blit();

  uart_puts("  total cycles=");
  putdec(total);
  uart_puts("\n");

  /* Liveness bar only.  The interesting output is the per-workload numbers
   * above compared across profiles, not a threshold here: a memory system can
   * be ten times better or worse and still be "working". */
  if (!total) { uart_puts("render_perf: FAIL no cycles\n"); test_finish(1); }
  uart_puts("render_perf: PASS\n");
  test_finish(0);
}
