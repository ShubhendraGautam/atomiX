#pragma once

#include <stdint.h>

#include "platform.h"

/* aXos in-kernel role driver — the shell + role contract (DESIGN.md §3.3) as
 * the management kernel sees it.  The fixed 64 KiB role window is device-mapped
 * into the kernel's S-mode address space by vm_bootstrap_map (identity, so the
 * physical base doubles as the kernel virtual address).  This is the first
 * piece of the shell control plane: aXos, not a bare-metal test program,
 * discovering and driving the accelerator.  The host-link service will call
 * this same driver on behalf of remote requests. */
#define AX_ROLE_ID       (AX_ROLE_BASE + 0x0000u)
#define AX_ROLE_VERSION  (AX_ROLE_BASE + 0x0004u)
#define AX_ROLE_DOORBELL (AX_ROLE_BASE + 0x0008u)
#define AX_ROLE_STATUS   (AX_ROLE_BASE + 0x000cu)

#define AX_ROLE_STATUS_BUSY 0x1u
#define AX_ROLE_STATUS_DONE 0x2u

/* Known role identities (ROLE_ID reads zero when no role is present). */
#define AX_ROLE_ID_LOOPBACK 0x4c4f4f50u /* "LOOP" */
#define AX_ROLE_ID_TPU      0x5450554cu /* "TPUL" */
#define AX_ROLE_ID_GPU      0x47505543u /* "GPUC" */

/* loopback descriptor registers — the universal contract-proof role, which the
 * kernel can drive end-to-end without any role-specific job encoding. */
#define AX_ROLE_LOOP_SRC (AX_ROLE_BASE + 0x0010u)
#define AX_ROLE_LOOP_DST (AX_ROLE_BASE + 0x0014u)
#define AX_ROLE_LOOP_LEN (AX_ROLE_BASE + 0x0018u)
#define AX_ROLE_LOOP_BUF (AX_ROLE_BASE + 0x1000u)

uint32_t role_discover(void);       /* ROLE_ID; 0 means no role present */
uint32_t role_version(void);
const char *role_name(uint32_t role_id);
int role_loopback_selftest(void);   /* 0 = copy verified, -1 = mismatch */

/* Drive one loopback copy over caller-supplied data: write `words` inputs into
 * the role buffer, run the copy, and read the results back.  Used by the
 * host-link service to run a job on behalf of a remote request. */
void role_loopback_copy(const uint32_t *in, uint32_t *out, uint32_t words);
