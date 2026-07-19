// Absent-role placeholder for the shell's fixed role window.  The window
// always decodes so discovery software can probe it safely: ROLE_ID reads as
// zero ("no role present"), every other read returns zero, and writes are
// accepted and dropped.  The bus never hangs and never errors here, which
// keeps role probing a plain load instead of a trap-handling exercise.
module axrole (
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
  always_comb begin
    i_ready = i_valid;
    i_rdata = 32'b0;
    i_err   = 1'b0;
    d_ready = d_valid;
    d_rdata = 32'b0;
    d_err   = 1'b0;
  end

  // verilator lint_off UNUSED
  wire unused_inputs = &{clk, rst, i_addr, i_wdata, i_wstrb,
                         d_addr, d_wdata, d_wstrb};
  // verilator lint_on UNUSED
endmodule
