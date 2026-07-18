# aXbus — interconnect specification

The single bus protocol of the atomiX SoC. Minimal synchronous
request/response, single outstanding transaction, deliberately a near-subset
of Wishbone classic (a bridge is a thin adapter). Both aXcore masters
(`ibus`, `dbus`) and every slave speak exactly this.

Status: **v1, normative for the reference system**. Changes require updating
this spec in the same commit.

## 1. Signals

All signals synchronous to the single system clock, active high. Reset:
masters must hold `valid` low.

Master → slave:

| Signal | Width | Meaning |
|---|---|---|
| `valid` | 1 | Request present; address/data/wstrb are valid |
| `addr` | 32 | Byte address |
| `wdata` | 32 | Write data (ignored for reads) |
| `wstrb` | 4 | Byte lane enables. `0000` = **read**; any set bit = **write** of those lanes |

Slave → master:

| Signal | Width | Meaning |
|---|---|---|
| `ready` | 1 | Transaction completes this cycle |
| `rdata` | 32 | Read data, valid only in the completing cycle of a read |
| `err` | 1 | Qualifies the completing cycle: transaction faulted |

## 2. Protocol rules

1. A transaction occupies every cycle in which `valid=1`, and **completes in
   the cycle where `valid && ready`**.
2. Once `valid` is raised, the master must hold `valid`, `addr`, `wdata`,
   `wstrb` **stable until the completing cycle** (no early deassertion, no
   address changes mid-wait).
3. `ready` may be combinational (same-cycle completion — BRAM) or registered
   (wait states — SDRAM, caches, slow peripherals). Masters must tolerate any
   number of wait cycles: **this is the provision that lets memories change
   underneath the core without core changes.**
4. Single outstanding transaction per master: a new `valid` may rise no
   earlier than the cycle after the previous completion.
5. `err=1` in the completing cycle means the access faulted (decode miss,
   slave-internal fault, write to ROM). `rdata` is undefined; the master
   raises the corresponding precise exception (access fault).
6. A slave must not assert `ready` in a cycle where `valid=0`.
7. Reads have no side effects on `err`; write completion with `err=1` means
   the write did not happen.
8. Sub-word writes use `wstrb` (e.g. `0001` = byte at addr[1:0]=0); sub-word
   reads return the full word — the master extracts/extends lanes. Addresses
   are word-aligned by the master (`addr[1:0]=0`); alignment faults are the
   master's job *before* the bus.

## 3. Timing examples

1-cycle slave (BRAM), read:

```
clk     ‾\_/‾\_/‾\_/‾
valid   __/‾‾‾\______
addr    --< A >------
ready   __/‾‾‾\______      completes in the same cycle
rdata   --< D >------
```

Slave with one wait state:

```
clk     ‾\_/‾\_/‾\_/‾\_/‾
valid   __/‾‾‾‾‾‾‾\______
addr    --<   A   >------
ready   ______/‾‾‾\______      master holds request; completes cycle 2
rdata   ------< D >------
```

Error completion (decode miss):

```
valid   __/‾‾‾\______
ready   __/‾‾‾\______
err     __/‾‾‾\______      master takes an access-fault exception
```

## 4. Topology

- `axbus_mux` (`components/interconnect/axbus-reference/`): address decode per
  DESIGN.md §3.2, routes one master to N slaves; decode miss completes with
  `err=1` (never hangs — rule for every slave too: bounded completion).
- v1 has two masters (`ibus`, `dbus`) hitting disjoint or dual-ported slaves;
  a true arbiter joins when a shared slave or a DMA master appears.

## 5. Wishbone mapping (for imported cores, later)

| aXbus | Wishbone classic |
|---|---|
| `valid` | `cyc & stb` |
| `wstrb != 0` / `== 0` | `we` + `sel` |
| `ready` | `ack` |
| `err` | `err` |

Single-outstanding + hold-stable maps onto Wishbone classic cycles 1:1.
