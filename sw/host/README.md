# sw/host/ — axhost host-side driver

The software that runs on your PC and makes the FPGA a managed accelerator
card.  It speaks the framed aX host-link protocol
([docs/host-protocol.md](../../docs/host-protocol.md)) to the aXos host-link
service on the shell — never role internals.

## What exists (base)

- [`axhost.py`](axhost.py) — the host driver: the host-link frame codec plus a
  **virtual-pipe backend** that talks to a Verilator simulation of the shell
  over the console byte pipe (request bytes in through the harness UART input,
  response bytes out on the model's stdout).  It runs PING, INFO, and a
  ROLE_RUN job against `role.loopback` and checks every response.  Evidence:
  `make -C sw/kernel check-hostlink`.

The aXos side is the host-link personality built with `HOSTLINK=1`
([sw/kernel/hostlink.c](../kernel/hostlink.c)), which dispatches frames to the
in-kernel role driver ([sw/kernel/role.c](../kernel/role.c)).

## What layers on next

- Per-role client libraries (e.g. `libaxtpu`: a matmul API that marshals
  tensors into a TPU-lite descriptor) above the frame codec.
- A **USB-serial backend** for real hardware, replacing `SimPipe` with the same
  codec, once the dedicated host-link channel exists (separate from the console;
  it needs a second byte-pipe peripheral and board pins, so it lands with
  ULX3S bring-up).
- New opcodes on the existing frame format: per-role job submission, buffer
  read/write and streaming, asynchronous completion, and bitstream upload for
  mode switching.

Design rule: `axhost` knows the **shell protocol only** — never role internals.
Role knowledge lives in aXos and in the per-role libraries.  Userspace only
(plain USB-serial); no kernel module unless PCIe ever happens.
