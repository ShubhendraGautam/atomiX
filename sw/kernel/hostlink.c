/* aXos host-link service — the shell control plane's host-facing side
 * (DESIGN.md §3.3).  It reads framed requests from the console byte pipe,
 * dispatches them to the in-kernel role driver (role.c), and writes framed
 * responses, so a host PC running axhost can discover and drive the
 * accelerator over the link.  The protocol is docs/host-protocol.md.
 *
 * This is the base: one request/response exchange at a time over the console
 * UART.  A dedicated USB-serial channel and per-role job opcodes layer on the
 * same frame codec without changing it. */
#include <stdint.h>

#include "hostlink.h"
#include "platform.h"
#include "role.h"

/* Frame payload staging.  Static rather than on-stack so the service keeps a
 * small, predictable footprint in the kernel's S-mode context. */
static uint8_t payload[HOSTLINK_MAX_PAYLOAD];
static uint32_t job_in[HOSTLINK_MAX_WORDS];
static uint32_t job_out[HOSTLINK_MAX_WORDS];
static int32_t  tpu_c[HOSTLINK_TPU_MAX_M * 8u];
static uint32_t gpu_prog[HOSTLINK_GPU_MAX_INSN];
static uint32_t gpu_data[HOSTLINK_GPU_MAX_DATA];
static uint32_t gpu_out[HOSTLINK_GPU_MAX_DATA];

static uint8_t get_byte(void) { return (uint8_t)uart_getchar(); }
static void put_byte(uint8_t b) { uart_putchar((char)b); }

static uint16_t get_u16(void) {
  const uint8_t lo = get_byte();
  const uint8_t hi = get_byte();
  return (uint16_t)(lo | ((uint16_t)hi << 8));
}

static void put_u32(uint8_t *p, uint32_t v) {
  p[0] = (uint8_t)v;
  p[1] = (uint8_t)(v >> 8);
  p[2] = (uint8_t)(v >> 16);
  p[3] = (uint8_t)(v >> 24);
}

static uint32_t get_u32(const uint8_t *p) {
  return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
         ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static void put_frame(uint8_t status, const uint8_t *data, uint16_t len) {
  put_byte(HOSTLINK_RSP_SYNC);
  put_byte(status);
  put_byte((uint8_t)len);
  put_byte((uint8_t)(len >> 8));
  for (uint16_t i = 0; i < len; ++i) put_byte(data[i]);
}

void host_service(void) {
  for (;;) {
    /* Resynchronize to the next request frame. */
    while (get_byte() != HOSTLINK_REQ_SYNC) {}
    const uint8_t op = get_byte();
    const uint16_t len = get_u16();

    /* Read the payload, draining (and rejecting) anything oversized. */
    if (len > sizeof(payload)) {
      for (uint16_t i = 0; i < len; ++i) (void)get_byte();
      put_frame(HOSTLINK_ST_BAD_LEN, 0, 0);
      continue;
    }
    for (uint16_t i = 0; i < len; ++i) payload[i] = get_byte();

    switch (op) {
      case HOSTLINK_OP_PING: {
        const uint8_t pong[4] = {'a', 'X', 'H', 'L'};
        put_frame(HOSTLINK_ST_OK, pong, 4);
        break;
      }
      case HOSTLINK_OP_INFO: {
        uint8_t info[8];
        put_u32(&info[0], role_discover());
        put_u32(&info[4], role_version());
        put_frame(HOSTLINK_ST_OK, info, 8);
        break;
      }
      case HOSTLINK_OP_ROLE_RUN: {
        if (role_discover() != AX_ROLE_ID_LOOPBACK) {
          put_frame(HOSTLINK_ST_NO_ROLE, 0, 0);
          break;
        }
        if (len < 2u) {
          put_frame(HOSTLINK_ST_BAD_LEN, 0, 0);
          break;
        }
        const uint16_t words = (uint16_t)(payload[0] | ((uint16_t)payload[1] << 8));
        if (words > HOSTLINK_MAX_WORDS || len != (uint16_t)(2u + 4u * words)) {
          put_frame(HOSTLINK_ST_BAD_LEN, 0, 0);
          break;
        }
        for (uint16_t i = 0; i < words; ++i)
          job_in[i] = get_u32(&payload[2u + 4u * i]);
        role_loopback_copy(job_in, job_out, words);
        for (uint16_t i = 0; i < words; ++i)
          put_u32(&payload[4u * i], job_out[i]);
        put_frame(HOSTLINK_ST_OK, payload, (uint16_t)(4u * words));
        break;
      }
      case HOSTLINK_OP_TPU_GEMM: {
        /* payload = m(1) | ctrl(1) | W[64] | A[8*m]; response = C[m*8] int32 */
        if (role_discover() != AX_ROLE_ID_TPU) {
          put_frame(HOSTLINK_ST_NO_ROLE, 0, 0);
          break;
        }
        if (len < 66u) {
          put_frame(HOSTLINK_ST_BAD_LEN, 0, 0);
          break;
        }
        const uint32_t m = payload[0];
        const uint32_t ctrl = payload[1];
        if (m == 0u || m > HOSTLINK_TPU_MAX_M || len != (uint16_t)(66u + 8u * m)) {
          put_frame(HOSTLINK_ST_BAD_LEN, 0, 0);
          break;
        }
        role_tpu_gemm((const int8_t *)&payload[2], (const int8_t *)&payload[66],
                      m, ctrl, tpu_c);
        for (uint32_t i = 0; i < m * 8u; ++i)
          put_u32(&payload[4u * i], (uint32_t)tpu_c[i]);
        put_frame(HOSTLINK_ST_OK, payload, (uint16_t)(4u * m * 8u));
        break;
      }
      case HOSTLINK_OP_GPU_RUN: {
        /* payload = nthreads(2) | ninsn(2) | ndata(2) | prog[ninsn] | data[ndata]
         * response = the data buffer read back (ndata words). */
        if (role_discover() != AX_ROLE_ID_GPU) {
          put_frame(HOSTLINK_ST_NO_ROLE, 0, 0);
          break;
        }
        if (len < 6u) {
          put_frame(HOSTLINK_ST_BAD_LEN, 0, 0);
          break;
        }
        const uint32_t nthreads = payload[0] | ((uint32_t)payload[1] << 8);
        const uint32_t ninsn = payload[2] | ((uint32_t)payload[3] << 8);
        const uint32_t ndata = payload[4] | ((uint32_t)payload[5] << 8);
        if (ninsn > HOSTLINK_GPU_MAX_INSN || ndata > HOSTLINK_GPU_MAX_DATA ||
            len != (uint16_t)(6u + 4u * ninsn + 4u * ndata)) {
          put_frame(HOSTLINK_ST_BAD_LEN, 0, 0);
          break;
        }
        for (uint32_t i = 0; i < ninsn; ++i)
          gpu_prog[i] = get_u32(&payload[6u + 4u * i]);
        for (uint32_t i = 0; i < ndata; ++i)
          gpu_data[i] = get_u32(&payload[6u + 4u * ninsn + 4u * i]);
        role_gpu_run(gpu_prog, ninsn, gpu_data, ndata, nthreads, gpu_out);
        for (uint32_t i = 0; i < ndata; ++i)
          put_u32(&payload[4u * i], gpu_out[i]);
        put_frame(HOSTLINK_ST_OK, payload, (uint16_t)(4u * ndata));
        break;
      }
      case HOSTLINK_OP_BYE:
        put_frame(HOSTLINK_ST_OK, 0, 0);
        test_finish(0);   /* acknowledged; end the session and the program */
        break;
      default:
        put_frame(HOSTLINK_ST_BAD_OP, 0, 0);
        break;
    }
  }
}
