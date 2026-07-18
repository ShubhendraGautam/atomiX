// Minimal 16550-compatible transmit/status subset. THR writes emit one byte
// on the synthesizable tx_* sideband; LSR reports the transmitter idle.
module uart #(
  parameter logic [31:0] BASE = 32'h1000_0000
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
  output logic        d_err,
  output logic        tx_valid,
  output logic [7:0]  tx_data
);
  wire i_in_range = i_addr >= BASE && i_addr - BASE < 32'h1000;
  wire d_in_range = d_addr >= BASE && d_addr - BASE < 32'h1000;
  wire [2:0] i_off = i_addr[2:0];
  wire [2:0] d_off = d_addr[2:0];
  // aXbus reads are word-aligned.  Return register bytes in their physical
  // lane so the core's normal load extractor sees UART offsets 2 and 5.
  function automatic logic [31:0] read_offset(input logic [2:0] off);
    unique case (off)
      3'd0:    read_offset = 32'h0001_0000;  // IIR at byte offset 2
      3'd4:    read_offset = 32'h0000_6000;  // LSR at byte offset 5
      default: read_offset = 32'b0;
    endcase
  endfunction

  always_comb begin
    i_ready = i_valid; i_err = i_valid && !i_in_range; i_rdata = read_offset(i_off);
    d_ready = d_valid; d_err = d_valid && !d_in_range; d_rdata = read_offset(d_off);
  end
  always_ff @(posedge clk) begin
    if (rst) begin
      tx_valid <= 1'b0;
      tx_data  <= 8'b0;
    end else begin
      tx_valid <= 1'b0;
      if (i_valid && i_off == 3'd0 && i_wstrb[0]) begin
        tx_valid <= 1'b1; tx_data <= i_wdata[7:0];
      end
      if (d_valid && d_off == 3'd0 && d_wstrb[0]) begin
        tx_valid <= 1'b1; tx_data <= d_wdata[7:0];
      end
    end
  end

  // The 16550 subset intentionally consumes only THR's low byte/lane.
  // verilator lint_off UNUSED
  wire unused_write_lanes = ^{i_wdata[31:8], i_wstrb[3:1],
                               d_wdata[31:8], d_wstrb[3:1]};
  // verilator lint_on UNUSED
endmodule
