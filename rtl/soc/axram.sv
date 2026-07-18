// Small dual-port byte-write RAM. Both ports implement the aXbus completion
// rule combinationally; writes take effect on the following rising edge.
module axram #(
  parameter logic [31:0] BASE = 32'h8000_0000,
  parameter int unsigned BYTES = 128 * 1024
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
  localparam int unsigned WORDS = BYTES / 4;
  localparam int unsigned INDEX_BITS = $clog2(WORDS);
  logic [31:0] mem [0:WORDS-1];
  wire [31:0] i_offset = i_addr - BASE;
  wire [31:0] d_offset = d_addr - BASE;
  wire i_ok = i_addr >= BASE && i_offset <= BYTES - 4 && i_addr[1:0] == 2'b00;
  wire d_ok = d_addr >= BASE && d_offset <= BYTES - 4 && d_addr[1:0] == 2'b00;
  wire [INDEX_BITS-1:0] i_index = i_offset[INDEX_BITS+1:2];
  wire [INDEX_BITS-1:0] d_index = d_offset[INDEX_BITS+1:2];

  always_comb begin
    i_ready = i_valid;
    i_err   = i_valid && !i_ok;
    i_rdata = i_ok ? mem[i_index] : 32'b0;
    d_ready = d_valid;
    d_err   = d_valid && !d_ok;
    d_rdata = d_ok ? mem[d_index] : 32'b0;
  end

  always_ff @(posedge clk) begin
    if (i_valid && i_ok && |i_wstrb) begin
      if (i_wstrb[0]) mem[i_index][7:0]   <= i_wdata[7:0];
      if (i_wstrb[1]) mem[i_index][15:8]  <= i_wdata[15:8];
      if (i_wstrb[2]) mem[i_index][23:16] <= i_wdata[23:16];
      if (i_wstrb[3]) mem[i_index][31:24] <= i_wdata[31:24];
    end
    if (d_valid && d_ok && |d_wstrb) begin
      if (d_wstrb[0]) mem[d_index][7:0]   <= d_wdata[7:0];
      if (d_wstrb[1]) mem[d_index][15:8]  <= d_wdata[15:8];
      if (d_wstrb[2]) mem[d_index][23:16] <= d_wdata[23:16];
      if (d_wstrb[3]) mem[d_index][31:24] <= d_wdata[31:24];
    end
  end

  // verilator lint_off UNUSED
  wire unused_rst = rst;
  // verilator lint_on UNUSED
endmodule
