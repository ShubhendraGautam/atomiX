// Reference memory selection for the stock aX SoC contract.
//
// `soc_top` talks only to the `axmem` module.  The default `axmem` wrapper
// instantiates this module, while a component configuration may instead
// provide another module named `axmem` with the same boundary.  This keeps the
// stock CPU/SoC path straightforward while leaving the memory implementation
// genuinely replaceable.
module axmem_reference #(
  parameter int unsigned RAM_BYTES = 128 * 1024,
  parameter int unsigned USE_DRAM_MODEL = 0,
  parameter int unsigned USE_SDRAM = 0,
  parameter RAM_INIT_FILE = ""
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
  output logic        sdram_cke,
  output logic        sdram_cs_n,
  output logic        sdram_ras_n,
  output logic        sdram_cas_n,
  output logic        sdram_we_n,
  output logic [1:0]  sdram_ba,
  output logic [12:0] sdram_a,
  output logic [1:0]  sdram_dqm,
  // verilator lint_off UNUSED
  input  logic [15:0] sdram_dq_i,
  // verilator lint_on UNUSED
  output logic [15:0] sdram_dq_o,
  output logic        sdram_dq_oe,
  output logic        sdram_init_done
);
  generate
    if (USE_SDRAM != 0) begin : g_sdram
      axsdram #(.BYTES(RAM_BYTES)) u_ram (
        .clk(clk), .rst(rst), .i_valid(i_valid), .i_addr(i_addr),
        .i_wdata(i_wdata), .i_wstrb(i_wstrb), .i_ready(i_ready),
        .i_rdata(i_rdata), .i_err(i_err), .d_valid(d_valid),
        .d_addr(d_addr), .d_wdata(d_wdata), .d_wstrb(d_wstrb),
        .d_ready(d_ready), .d_rdata(d_rdata), .d_err(d_err),
        .sdram_cke(sdram_cke), .sdram_cs_n(sdram_cs_n),
        .sdram_ras_n(sdram_ras_n), .sdram_cas_n(sdram_cas_n),
        .sdram_we_n(sdram_we_n), .sdram_ba(sdram_ba), .sdram_a(sdram_a),
        .sdram_dqm(sdram_dqm), .sdram_dq_i(sdram_dq_i),
        .sdram_dq_o(sdram_dq_o), .sdram_dq_oe(sdram_dq_oe),
        .init_done(sdram_init_done)
      );
    end else if (USE_DRAM_MODEL != 0) begin : g_dram
      axdram_model #(.BYTES(RAM_BYTES), .INIT_FILE(RAM_INIT_FILE)) u_ram (
        .clk(clk), .rst(rst), .i_valid(i_valid), .i_addr(i_addr),
        .i_wdata(i_wdata), .i_wstrb(i_wstrb), .i_ready(i_ready),
        .i_rdata(i_rdata), .i_err(i_err), .d_valid(d_valid),
        .d_addr(d_addr), .d_wdata(d_wdata), .d_wstrb(d_wstrb),
        .d_ready(d_ready), .d_rdata(d_rdata), .d_err(d_err)
      );
    end else begin : g_bram
      axram #(.BYTES(RAM_BYTES), .INIT_FILE(RAM_INIT_FILE)) u_ram (
        .clk(clk), .rst(rst), .i_valid(i_valid), .i_addr(i_addr),
        .i_wdata(i_wdata), .i_wstrb(i_wstrb), .i_ready(i_ready),
        .i_rdata(i_rdata), .i_err(i_err), .d_valid(d_valid),
        .d_addr(d_addr), .d_wdata(d_wdata), .d_wstrb(d_wstrb),
        .d_ready(d_ready), .d_rdata(d_rdata), .d_err(d_err)
      );
    end

    if (USE_SDRAM == 0) begin : g_no_sdram_pins
      assign sdram_cke = 1'b0;
      assign sdram_cs_n = 1'b1;
      assign sdram_ras_n = 1'b1;
      assign sdram_cas_n = 1'b1;
      assign sdram_we_n = 1'b1;
      assign sdram_ba = 2'b00;
      assign sdram_a = 13'b0;
      assign sdram_dqm = 2'b00;
      assign sdram_dq_o = 16'b0;
      assign sdram_dq_oe = 1'b0;
      assign sdram_init_done = 1'b0;
    end
  endgenerate
endmodule
