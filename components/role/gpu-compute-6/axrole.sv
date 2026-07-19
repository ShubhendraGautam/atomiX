// GPU-compute-6 role: the SIMT vector engine at 6 lanes, sized to pair with the
// minimal host core (core.minimal) on a small FPGA — the accelerator-first
// build where a tiny control CPU drives a wide engine (docs/tangnano-capacity.md).
//
// Same engine, ISA, window layout, and driver contract as role.gpu-compute
// (see gpu_engine.sv); only NLANES differs.  Six lanes is not a power of two;
// the engine forms thread ids by addition, so this is purely a lane-count
// change.  Conflict-free kernels are bit-identical to the 8-lane reference.
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
  gpu_engine #(.BASE(BASE), .NLANES(6)) u_engine (.*);
endmodule
