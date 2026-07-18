// Simulation-only compatible sifive_test endpoint. It stays synthesizable so
// a board build can simply leave its outputs unconnected.
module test_finisher (
  input  logic        clk,
  input  logic        rst,
  input  logic        i_valid,
  input  logic [31:0] i_wdata,
  input  logic [3:0]  i_wstrb,
  output logic        i_ready,
  output logic [31:0] i_rdata,
  output logic        i_err,
  input  logic        d_valid,
  input  logic [31:0] d_wdata,
  input  logic [3:0]  d_wstrb,
  output logic        d_ready,
  output logic [31:0] d_rdata,
  output logic        d_err,
  output logic        finished,
  output logic [15:0] exit_code
);
  always_comb begin
    i_ready = i_valid; i_rdata = 32'b0; i_err = 1'b0;
    d_ready = d_valid; d_rdata = 32'b0; d_err = 1'b0;
  end
  always_ff @(posedge clk) begin
    if (rst) begin
      finished <= 1'b0;
      exit_code <= 16'b0;
    end else begin
      if (i_valid && |i_wstrb &&
          (i_wdata[15:0] == 16'h5555 || i_wdata[15:0] == 16'h7777 ||
           i_wdata[15:0] == 16'h3333)) begin
        finished <= 1'b1;
        exit_code <= i_wdata[15:0] == 16'h3333 ? i_wdata[31:16] : 16'b0;
      end
      if (d_valid && |d_wstrb &&
          (d_wdata[15:0] == 16'h5555 || d_wdata[15:0] == 16'h7777 ||
           d_wdata[15:0] == 16'h3333)) begin
        finished <= 1'b1;
        exit_code <= d_wdata[15:0] == 16'h3333 ? d_wdata[31:16] : 16'b0;
      end
    end
  end
endmodule
