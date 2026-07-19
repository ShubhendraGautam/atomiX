# True partial reconfiguration of the role region (research track)

Goal: replace the **role** region of a running ULX3S bitstream — swap a TPU
role for a GPU role — without reloading the full bitstream and without
rebooting the shell (aXcore + aXos + peripherals keep running).

This is a research track, not a verified capability.  Nothing below is a
platform claim until the listed evidence exists.  The runtime-selection story
that already works — role discovery through the fixed `0x4000_0000` window and
role behavior driven by runtime-loaded descriptors/programs — does not depend
on this track.

## Why this is plausible on ECP5 with open tools

Findings from the Project Trellis documentation and tools (checked July 2026):

- The ECP5 bitstream format documentation states that `LSC_WRITE_ADDRESS`
  "can be used to make partial bitstreams.  Combined with background
  reconfiguration and the ability to reload frames glitchlessly; partial
  reconfiguration is possible on ECP5."
- Loading a partial bitstream requires the `BACKGROUND_RECONFIG` sysCONFIG
  option in the resident design, then a JTAG preamble (instruction `0x79`
  with no data, then `0x74` followed by `0x00`) before the partial data.
- `ecppack --delta <reference.config>` already exists: it compares the
  configuration RAM frame-by-frame against a reference design and emits a
  bitstream containing only the differing frames.
- Configuration frames are column-shaped (106-frame groups per column), so a
  role region confined to whole columns has its own frames.
- nextpnr has per-cell placement constraints (a `Bel` attribute per cell) and
  a Python API that can apply them programmatically; it does **not** have a
  first-class region/pblock floorplanning flow.  This is the research gap.

Sources: the [Trellis bitstream-format
documentation](https://prjtrellis.readthedocs.io/en/latest/architecture/bitstream_format.html),
[`ecppack.cpp`](https://github.com/f4pga/prjtrellis/blob/master/libtrellis/tools/ecppack.cpp),
and the [nextpnr constraints
documentation](https://github.com/YosysHQ/nextpnr/blob/master/docs/constraints.md).
The Trellis documentation also notes frame addressing is fully documented
only for the 45k device; the ULX3S-85F may need fuzzing work.

## How the role contract already prepares for this

- **Fixed window, discovery by ID**: after a swap, software re-reads
  `ROLE_ID` at `0x4000_0000`; the new role identifies itself.  No shell
  address map change is ever part of a role swap.
- **Quiesce protocol**: `STATUS.BUSY` defines "safe to swap" — the driver
  waits for idle and stops issuing doorbells before reconfiguring.
- **Planned shell isolation register**: before rewriting fabric, the shell
  must fence the role window (force ready/zero data at the mux boundary, the
  moral equivalent of Xilinx DFX decoupling) so a half-configured role cannot
  wedge aXbus.  This lands with the host-link/PLIC work.

## Staged plan and evidence gates

1. **Baseline (blocked on the existing ULX3S gate)**: run ECP5
   place-and-route, program SRAM, boot aXos on the board.  Without a running
   full bitstream there is nothing to partially reconfigure.
2. **Delta measurement (no live reconfig yet)**: build `shell + role.none`
   and `shell + role.loopback` with identical seeds; run
   `ecppack --delta` between them; count differing frames and check how many
   fall outside any plausible role region.  Expectation: without placement
   locking the delta touches shell frames all over the die.
3. **Shell locking experiment**: use the nextpnr Python API to pin every
   shell cell to the BELs of a reference run and confine role cells to
   reserved columns; iterate until the delta touches only role-region
   frames.  Routing divergence is the expected hard part; measure it, don't
   assume it.
4. **Live-load experiment (SRAM only, board at hand)**: set
   `BACKGROUND_RECONFIG`, quiesce the role, send the JTAG preamble plus the
   delta bitstream, re-run discovery.  Success = aXos never stops running
   (UART session survives) and the new `ROLE_ID` appears.  Failure modes
   (device reinit, bus wedge) are recoverable by full SRAM reprogram.
5. **Only then** promote the capability into
   [design-checklist.md](design-checklist.md) with the recorded commands.

Until stage 4 passes on hardware, "swap without reflashing" on the physical
board means full-bitstream SRAM reload (~1 s, no flash wear); in simulation
it means selecting a different role component and rebuilding the model.
