#pragma once

/* aX host-link protocol v0 — the shell control-plane wire format.  The
 * authoritative spec is docs/host-protocol.md; keep this and sw/host/axhost.py
 * in step with it.  Transport is a byte pipe (the console UART in this base;
 * a dedicated USB-serial channel later). */
#define HOSTLINK_REQ_SYNC 0xa5u
#define HOSTLINK_RSP_SYNC 0x5au

#define HOSTLINK_OP_PING     0x01u
#define HOSTLINK_OP_INFO     0x02u
#define HOSTLINK_OP_ROLE_RUN 0x10u
#define HOSTLINK_OP_BYE      0x7fu

#define HOSTLINK_ST_OK      0x00u
#define HOSTLINK_ST_BAD_OP  0x01u
#define HOSTLINK_ST_BAD_LEN 0x02u
#define HOSTLINK_ST_NO_ROLE 0x03u

/* ROLE_RUN payload cap for the base (keeps the frame buffers small). */
#define HOSTLINK_MAX_WORDS 62u

/* Run the host-link service over the console byte pipe: read framed requests,
 * dispatch them to the in-kernel role driver, and write framed responses.
 * Ends the session (and the program) on a BYE request. */
void host_service(void);
