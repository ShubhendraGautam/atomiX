// GPU-compute-16 role: the SIMT vector engine at 16 lanes, for large parts with
// LUT/DSP headroom (e.g. the ULX3S-85F, at ~26% used with 8 lanes).  Doubling
// the lanes doubles the parallel ALU/multiply throughput; memory-bound kernels
// do not speed up further because LDX/STX still serialise the lanes through the
// engine's single global-buffer port.
//
// Same engine, ISA, window layout, and driver contract as role.gpu-compute
// (see gpu_engine.sv); only NLANES differs.  Conflict-free kernels are
// bit-identical to the 8-lane reference.
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
  gpu_engine #(.BASE(BASE), .NLANES(16)) u_engine (.*);
endmodule
