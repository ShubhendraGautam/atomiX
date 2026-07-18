// Default stock-SoC memory component.  A component configuration can replace
// this file with any implementation that provides the `axmem` boundary.
module axmem #(
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
  input  logic [15:0] sdram_dq_i,
  output logic [15:0] sdram_dq_o,
  output logic        sdram_dq_oe,
  output logic        sdram_init_done
);
  axmem_reference #(
    .RAM_BYTES(RAM_BYTES), .USE_DRAM_MODEL(USE_DRAM_MODEL),
    .USE_SDRAM(USE_SDRAM), .RAM_INIT_FILE(RAM_INIT_FILE)
  ) u_reference (.*);
endmodule
