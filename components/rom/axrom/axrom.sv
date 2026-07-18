// Dual-port read-only boot ROM. Writes complete with an access error.
module axrom #(
  parameter logic [31:0] BASE = 32'h0000_1000,
  parameter int unsigned BYTES = 4096,
  parameter INIT_FILE = ""
) (
  input  logic        clk,
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

  // verilator lint_off WIDTH
  initial if (INIT_FILE) $readmemh(INIT_FILE, mem);
  // verilator lint_on WIDTH

  always_comb begin
    i_ready = i_valid;
    i_err   = i_valid && (!i_ok || |i_wstrb);
    i_rdata = i_ok ? mem[i_index] : 32'b0;
    d_ready = d_valid;
    d_err   = d_valid && (!d_ok || |d_wstrb);
    d_rdata = d_ok ? mem[d_index] : 32'b0;
  end

  // verilator lint_off UNUSED
  wire unused_inputs = ^{clk, i_wdata, d_wdata};
  // verilator lint_on UNUSED
endmodule
