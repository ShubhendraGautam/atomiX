// ax2_regfile: a 4-read / 2-write RV32I register file.
//
// A dual-issue core reads four operands and retires two results per cycle, so
// the stock 2R1W `regfile` component cannot serve it.  The array is kept in
// flip-flops (1024 of them) rather than block RAM: FPGA block RAM offers two
// ports, not six, and a BRAM-based file would need four banked copies plus
// write replication -- more fabric than the flops it saves, at this size.
//
// Write-first semantics: a read of a register being written this cycle returns
// the new value.  That removes the write-to-read bypass the pipeline would
// otherwise need in front of every operand.
//
// Port 1 has priority over port 0.  Both the array update and the read bypass
// honour that, so when two co-issued instructions name the same rd the younger
// one (slot 1) wins -- which is the in-order architectural result.
module ax2_regfile (
  input  logic        clk,

  input  logic        we0,
  input  logic [4:0]  waddr0,
  input  logic [31:0] wdata0,
  input  logic        we1,
  input  logic [4:0]  waddr1,
  input  logic [31:0] wdata1,

  input  logic [4:0]  raddr0,
  output logic [31:0] rdata0,
  input  logic [4:0]  raddr1,
  output logic [31:0] rdata1,
  input  logic [4:0]  raddr2,
  output logic [31:0] rdata2,
  input  logic [4:0]  raddr3,
  output logic [31:0] rdata3
);
  logic [31:0] xr [1:31];

  // x0 is hardwired zero, so writes to it are dropped rather than stored.
  wire w0 = we0 && waddr0 != 5'd0;
  wire w1 = we1 && waddr1 != 5'd0;

  always_ff @(posedge clk) begin
    if (w0) xr[waddr0] <= wdata0;
    if (w1) xr[waddr1] <= wdata1;   // later assignment wins on an address tie
  end

  function automatic logic [31:0] rd_port(input logic [4:0] a);
    if (a == 5'd0)                 rd_port = 32'b0;
    else if (w1 && waddr1 == a)    rd_port = wdata1;
    else if (w0 && waddr0 == a)    rd_port = wdata0;
    else                           rd_port = xr[a];
  endfunction

  always_comb begin
    rdata0 = rd_port(raddr0);
    rdata1 = rd_port(raddr1);
    rdata2 = rd_port(raddr2);
    rdata3 = rd_port(raddr3);
  end
endmodule
