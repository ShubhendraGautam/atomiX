// GPU-compute-lite role: the SIMT vector engine at a reduced lane count so the
// CPU + GPU fit a small FPGA (e.g. the Tang Nano 20K / GW2A-18C, where the full
// 8-lane engine overflows the LUT budget — see docs/tangnano-capacity.md).
//
// This is the same programmable engine, ISA, window layout, and driver contract
// as role.gpu-compute (documented there and in gpu_engine.sv); only NLANES
// differs.  Fewer lanes means more waves per job (ceil(NTHREADS/NLANES)), so a
// job takes proportionally more cycles, but the result of a conflict-free
// kernel is bit-identical — the shipped kernels and the gpu.c oracle compare
// final buffer contents, which are lane-count-independent.
module axrole #(
  parameter logic [31:0] BASE = 32'h4000_0000
) (
  input  logic        clk,
  input  logic        rst,
  input  logic        i_valid,
  input  logic [31:0] i_addr,
  input  logic [31:0] i_wdata,
  input  logic [3:0]  i_wstrb,
  output logic        i_ready,
  output logic [31:0] i_rdata,
  output logic        i_err,
  input  logic        d_valid,
  input  logic [31:0] d_addr,
  input  logic [31:0] d_wdata,
  input  logic [3:0]  d_wstrb,
  output logic        d_ready,
  output logic [31:0] d_rdata,
  output logic        d_err
);
  gpu_engine #(.BASE(BASE), .NLANES(4)) u_engine (.*);
endmodule
