// aXcore register file — DESIGN.md §4.2.
// 32 x 32-bit flip-flops, x0 hardwired to zero, 2 read + 1 write port.
//
// BYPASS=1 (default): write-before-read bypass — a read of the register being
// written this cycle returns the new value, so an instruction in ID sees what
// WB writes in the same cycle (the pipeline forwarding path people forget).
// BYPASS=0: plain synchronous write, no bypass — the correct choice for a
// multi-cycle core where one instruction both reads its sources and writes its
// destination in the same cycle (bypass there would be a combinational loop and
// a wrong self-reference).
module regfile #(
  parameter bit BYPASS = 1'b1
) (
  input  logic        clk,

  input  logic        we,
  input  logic [4:0]  waddr,
  input  logic [31:0] wdata,

  input  logic [4:0]  raddr1,
  output logic [31:0] rdata1,
  input  logic [4:0]  raddr2,
  output logic [31:0] rdata2
);

  logic [31:0] regs [31:1];

  always_ff @(posedge clk) begin
    if (we && waddr != 5'd0)
      regs[waddr] <= wdata;
  end

  always_comb begin
    if (raddr1 == 5'd0)                         rdata1 = 32'd0;
    else if (BYPASS && we && waddr == raddr1)   rdata1 = wdata;   // bypass
    else                                        rdata1 = regs[raddr1];

    if (raddr2 == 5'd0)                         rdata2 = 32'd0;
    else if (BYPASS && we && waddr == raddr2)   rdata2 = wdata;   // bypass
    else                                        rdata2 = regs[raddr2];
  end

endmodule
