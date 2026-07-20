// A transparent aXbus cache component for bring-up, timing comparison, or a
// user design that wants the stock SoC's cache seam without line storage. It
// has the same module and transaction contract as axcache, but forwards every
// request directly to memory and treats flush as a local no-op.
module axcache #(
  parameter logic [31:0] CACHE_BASE = 32'h8000_0000,
  parameter int unsigned CACHE_BYTES = 32 * 1024 * 1024,
  parameter int unsigned LINES = 16,
  parameter int unsigned WORDS_PER_LINE = 4
) (
  input  logic        clk,
  input  logic        rst,
  input  logic        flush,
  output logic        flush_busy,
  input  logic        c_valid,
  input  logic [31:0] c_addr,
  input  logic [31:0] c_wdata,
  input  logic [3:0]  c_wstrb,
  output logic        c_ready,
  output logic [31:0] c_rdata,
  output logic        c_err,
  output logic        m_valid,
  output logic [31:0] m_addr,
  output logic [31:0] m_wdata,
  output logic [3:0]  m_wstrb,
  input  logic        m_ready,
  input  logic [31:0] m_rdata,
  input  logic        m_err
);
  // Nothing is cached, so there is never anything to drain.
  assign flush_busy = 1'b0;
  always_comb begin
    m_valid = c_valid;
    m_addr = c_addr;
    m_wdata = c_wdata;
    m_wstrb = c_wstrb;
    c_ready = m_ready;
    c_rdata = m_rdata;
    c_err = m_err;
  end

  // verilator lint_off UNUSED
  wire unused_controls = ^{clk, rst, flush, CACHE_BASE, CACHE_BYTES, LINES,
                            WORDS_PER_LINE};
  // verilator lint_on UNUSED
endmodule
