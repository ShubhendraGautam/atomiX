# aX host-link protocol (v0)

The wire contract between `axhost` on the host PC and the aXos host-link
service on the FPGA shell (DESIGN.md §3.3).  This is the **base**: a minimal,
functionally complete request/response protocol that proves the whole
host → shell → role control plane end-to-end.  Richer capability (per-role job
submission, streaming buffers, asynchronous completion, flow control, CRC) is
layered on this frame format without breaking it.

## Transport

The protocol is a byte stream; it does not care what carries it.

- **Base (today):** the shell's console byte pipe.  In simulation this is the
  Verilator harness UART — `axhost` writes request bytes to `UART_INPUT_FILE`
  and reads response bytes from the model's stdout (the "virtual pipe" backend).
- **Enhancement (hardware):** a dedicated USB-serial channel, separate from the
  console, so a human console and the host daemon coexist.  That needs a second
  byte-pipe peripheral (an interconnect-mux port and board pins), so it lands
  with ULX3S bring-up.  Only the transport changes; the frames below do not.

In the base, aXos runs a **host-link personality** (built with `HOSTLINK=1`)
that speaks this protocol instead of the interactive shell.  The two are
unified into one concurrent image once the dedicated channel exists.

## Frames

All multi-byte integers are little-endian.

```
Request   A5 | op(1) | len(2) | payload(len)
Response  5A | status(1) | len(2) | payload(len)
```

- `A5` / `5A` are the request/response sync bytes.  A receiver resynchronizes
  by scanning for its sync byte, so a corrupt or partial frame cannot desync
  the stream permanently.
- `status` is `0` on success; nonzero is an error code (below).
- `len` is the payload byte count, `0..65535` (the base caps `ROLE_RUN` data at
  `HOSTLINK_MAX_WORDS` words).

## Opcodes (v0)

| op   | name       | request payload                     | ok response payload            |
|------|------------|-------------------------------------|--------------------------------|
| 0x01 | `PING`     | none                                | 4 bytes: `61 58 48 4C` (`aXHL`) |
| 0x02 | `INFO`     | none                                | `role_id`(u32) · `version`(u32) |
| 0x10 | `ROLE_RUN` | `words`(u16) · `words`×u32 input    | `words`×u32 result             |
| 0x7F | `BYE`      | none                                | none (then the session ends)   |

- `PING` proves the transport and framing round-trip.
- `INFO` returns what `ROLE_ID`/`VERSION` read through the role window
  (`role_id == 0` means no role is present).
- `ROLE_RUN` is the base's proof that the host can drive the accelerator: aXos
  loads the input words into the role and returns the result.  In v0 the target
  is `role.loopback` (the universal contract-proof role), so the result is the
  input copied through the engine.  Per-role job ops (a GEMM descriptor for
  TPU-lite, a SIMT kernel for GPU-compute) are added as new opcodes on this
  same frame format.
- `BYE` lets the host end the session cleanly; aXos acknowledges, then halts.

## Status codes

| code | meaning                                            |
|------|----------------------------------------------------|
| 0x00 | `OK`                                                |
| 0x01 | `BAD_OP` — unknown opcode                            |
| 0x02 | `BAD_LEN` — payload length invalid for the opcode    |
| 0x03 | `NO_ROLE` — op needs a role the shell does not have  |

## Authority

`sw/kernel/include/hostlink.h` (aXos side) and `sw/host/axhost.py` (host side)
implement this document; keep both in step with it.  Evidence:
`make -C sw/kernel check-hostlink` runs `axhost` against the simulated shell
with `role.loopback` and checks every response.
