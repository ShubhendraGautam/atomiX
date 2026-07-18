// Test-only composition: cache backed by the same delayed RAM model used by
// the Phase 6 SoC configuration.  Exposing the lower bus lets the C++ test
// prove hits do not generate extra external-memory transactions.
module axcache_test_top (
  input  logic clk,
  input  logic rst,
  input  logic flush,
  input  logic c_valid,
  input  logic [31:0] c_addr,
  input  logic [31:0] c_wdata,
  input  logic [3:0] c_wstrb,
  output logic c_ready,
  output logic [31:0] c_rdata,
  output logic c_err,
  output logic mem_valid,
  output logic mem_ready
);
  logic [31:0] mem_addr, mem_wdata, mem_rdata;
  logic [3:0] mem_wstrb;
  logic mem_err;

  axcache #(.CACHE_BASE(32'h8000_0000), .CACHE_BYTES(64), .LINES(4), .WORDS_PER_LINE(4)) u_cache (
    .clk(clk), .rst(rst), .flush(flush),
    .c_valid(c_valid), .c_addr(c_addr), .c_wdata(c_wdata), .c_wstrb(c_wstrb),
    .c_ready(c_ready), .c_rdata(c_rdata), .c_err(c_err),
    .m_valid(mem_valid), .m_addr(mem_addr), .m_wdata(mem_wdata), .m_wstrb(mem_wstrb),
    .m_ready(mem_ready), .m_rdata(mem_rdata), .m_err(mem_err)
  );

  // verilator lint_off PINCONNECTEMPTY
  axdram_model #(.BASE(32'h8000_0000), .BYTES(256), .LATENCY(2)) u_memory (
    .clk(clk), .rst(rst),
    .i_valid(mem_valid), .i_addr(mem_addr), .i_wdata(mem_wdata), .i_wstrb(mem_wstrb),
    .i_ready(mem_ready), .i_rdata(mem_rdata), .i_err(mem_err),
    .d_valid(1'b0), .d_addr(32'b0), .d_wdata(32'b0), .d_wstrb(4'b0),
    .d_ready(), .d_rdata(), .d_err()
  );
  // verilator lint_on PINCONNECTEMPTY
endmodule
