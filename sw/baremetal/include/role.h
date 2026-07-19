#pragma once

#include "platform.h"

/* Shell + role contract (DESIGN.md §3.3): one fixed 64 KiB aXbus window.
 * Software discovers the role by reading ROLE_ID; zero means no role is
 * present.  Offsets past AX_ROLE_COUNT are defined by the selected role. */
#define AX_ROLE_BASE 0x40000000u

#define AX_ROLE_ID       (AX_ROLE_BASE + 0x0000u)
#define AX_ROLE_VERSION  (AX_ROLE_BASE + 0x0004u)
#define AX_ROLE_DOORBELL (AX_ROLE_BASE + 0x0008u)
#define AX_ROLE_STATUS   (AX_ROLE_BASE + 0x000cu)

#define AX_ROLE_STATUS_BUSY 0x1u
#define AX_ROLE_STATUS_DONE 0x2u

/* role.loopback programming model. */
#define AX_ROLE_LOOP_ID    0x4c4f4f50u /* "LOOP" */
#define AX_ROLE_LOOP_SRC   (AX_ROLE_BASE + 0x0010u)
#define AX_ROLE_LOOP_DST   (AX_ROLE_BASE + 0x0014u)
#define AX_ROLE_LOOP_LEN   (AX_ROLE_BASE + 0x0018u)
#define AX_ROLE_LOOP_COUNT (AX_ROLE_BASE + 0x001cu)
#define AX_ROLE_LOOP_BUF   (AX_ROLE_BASE + 0x1000u)

/* role.tpu-lite programming model: C = acc_base + A x W, int8 operands,
 * int32 results.  W is the stationary 8x8 tile; A is M x 8 (M <= 256);
 * rows pack one int8 per byte, little-endian, two words per row. */
#define AX_ROLE_TPU_ID    0x5450554cu /* "TPUL" */
#define AX_ROLE_TPU_CTRL  (AX_ROLE_BASE + 0x0010u)
#define AX_ROLE_TPU_M     (AX_ROLE_BASE + 0x0014u)
#define AX_ROLE_TPU_COUNT (AX_ROLE_BASE + 0x0018u)
#define AX_ROLE_TPU_W     (AX_ROLE_BASE + 0x0100u)
#define AX_ROLE_TPU_A     (AX_ROLE_BASE + 0x1000u)
#define AX_ROLE_TPU_C     (AX_ROLE_BASE + 0x2000u)

#define AX_ROLE_TPU_CTRL_RELU 0x1u
#define AX_ROLE_TPU_CTRL_ACC  0x2u

/* role.gpu-compute programming model: an 8-lane SIMT vector engine.  Upload a
 * straight-line kernel to PROG and input data to DATA, set NTHREADS/NINSN, and
 * ring the doorbell; lane L of wave w runs thread tid = 8*w + L, with tid >=
 * NTHREADS predicated off (its stores drop, its loads read zero).  Registers
 * are undefined at kernel entry — a kernel must write a register before it
 * reads it.  DATA is a flat word-addressed global buffer the kernel indexes. */
#define AX_ROLE_GPU_ID       0x47505543u /* "GPUC" */
#define AX_ROLE_GPU_NTHREADS (AX_ROLE_BASE + 0x0010u)
#define AX_ROLE_GPU_NINSN    (AX_ROLE_BASE + 0x0014u)
#define AX_ROLE_GPU_COUNT    (AX_ROLE_BASE + 0x0018u)
#define AX_ROLE_GPU_PROG     (AX_ROLE_BASE + 0x0100u)
#define AX_ROLE_GPU_DATA     (AX_ROLE_BASE + 0x1000u)

/* Instruction opcodes (see components/role/gpu-compute/axrole.sv). */
#define AX_GPU_HALT 0u
#define AX_GPU_TID  1u
#define AX_GPU_LI   2u
#define AX_GPU_MOV  3u
#define AX_GPU_LDX  4u
#define AX_GPU_STX  5u
#define AX_GPU_ADD  6u
#define AX_GPU_SUB  7u
#define AX_GPU_MUL  8u
#define AX_GPU_AND  9u
#define AX_GPU_OR   10u
#define AX_GPU_XOR  11u
#define AX_GPU_SLL  12u
#define AX_GPU_SRL  13u
#define AX_GPU_SRA  14u
#define AX_GPU_MIN  15u
#define AX_GPU_MAX  16u
#define AX_GPU_ADDI 17u
#define AX_GPU_MULI 18u

/* Encode one 32-bit kernel instruction: op[31:26] rd[25:23] ra[22:20]
 * rb[19:17] imm[16:0]. */
static inline uint32_t gpu_insn(uint32_t op, uint32_t rd, uint32_t ra,
                                uint32_t rb, int32_t imm) {
  return (op << 26) | (rd << 23) | (ra << 20) | (rb << 17) |
         ((uint32_t)imm & 0x1ffffu);
}

static inline uint32_t mmio_read32(uint32_t addr) {
  return *(volatile const uint32_t *)(uintptr_t)addr;
}

static inline uint32_t role_id(void) { return mmio_read32(AX_ROLE_ID); }

static inline void role_ring_doorbell(void) {
  mmio_write32(AX_ROLE_DOORBELL, 1u);
}

static inline void role_wait_done(void) {
  while (!(mmio_read32(AX_ROLE_STATUS) & AX_ROLE_STATUS_DONE)) {}
}
