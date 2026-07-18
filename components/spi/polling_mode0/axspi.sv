// Polling SPI mode-0 controller for the Phase 6 SD-card path.  Software owns
// chip-select and launches one byte at a time; that deliberately mirrors the
// command/response sequences of an SPI-mode SD card without hiding protocol
// state in the hardware.  Both aXbus ports are supported; simultaneous writes
// are deterministic with the D port taking precedence.
module axspi #(
  parameter logic [31:0] BASE = 32'h1001_0000
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
  output logic        spi_sclk,
  output logic        spi_mosi,
  output logic        spi_cs_n,
  input  logic        spi_miso
);
  localparam logic [3:0] DATA = 4'h0, CTRL = 4'h4, STATUS = 4'h8,
                           CLKDIV = 4'hc;
  logic [7:0] tx_data_q, tx_shift_q, rx_data_q;
  logic [6:0] rx_shift_q;
  logic [15:0] clkdiv_q, div_count_q;
  logic [2:0] bit_count_q;
  logic busy_q, rx_valid_q, cs_n_q, sclk_q;

  wire i_in_range = i_addr >= BASE && i_addr - BASE < 32'h1000;
  wire d_in_range = d_addr >= BASE && d_addr - BASE < 32'h1000;
  wire [3:0] i_off = i_addr[3:0];
  wire [3:0] d_off = d_addr[3:0];
  function automatic logic known_offset(input logic [3:0] off);
    known_offset = off == DATA || off == CTRL || off == STATUS || off == CLKDIV;
  endfunction
  function automatic logic [31:0] read_offset(input logic [3:0] off);
    unique case (off)
      DATA:   read_offset = {24'b0, rx_data_q};
      CTRL:   read_offset = {30'b0, cs_n_q, 1'b0};
      STATUS: read_offset = {30'b0, rx_valid_q, busy_q};
      CLKDIV: read_offset = {16'b0, clkdiv_q};
      default: read_offset = 32'b0;
    endcase
  endfunction

  wire i_bad = !i_in_range || i_addr[11:4] != 8'b0 || i_addr[1:0] != 2'b00 || !known_offset(i_off) ||
               (i_valid && |i_wstrb && i_off == CTRL && i_wdata[0] && busy_q);
  wire d_bad = !d_in_range || d_addr[11:4] != 8'b0 || d_addr[1:0] != 2'b00 || !known_offset(d_off) ||
               (d_valid && |d_wstrb && d_off == CTRL && d_wdata[0] && busy_q);
  always_comb begin
    i_ready = i_valid;
    i_err = i_valid && i_bad;
    i_rdata = read_offset(i_off);
    d_ready = d_valid;
    d_err = d_valid && d_bad;
    d_rdata = read_offset(d_off);
    spi_sclk = sclk_q;
    spi_mosi = tx_shift_q[7];
    spi_cs_n = cs_n_q;
  end

  always_ff @(posedge clk) begin
    if (rst) begin
      tx_data_q <= 8'b0;
      tx_shift_q <= 8'b0;
      rx_shift_q <= 7'b0;
      rx_data_q <= 8'b0;
      clkdiv_q <= 16'd0;
      div_count_q <= 16'd0;
      bit_count_q <= 3'd0;
      busy_q <= 1'b0;
      rx_valid_q <= 1'b0;
      cs_n_q <= 1'b1;
      sclk_q <= 1'b0;
    end else begin
      // Reads consume the completed receive byte. D port wins if both ports
      // read DATA on one edge, matching the peripheral write priority below.
      if (i_valid && !i_err && i_wstrb == 4'b0 && i_off == DATA) rx_valid_q <= 1'b0;
      if (d_valid && !d_err && d_wstrb == 4'b0 && d_off == DATA) rx_valid_q <= 1'b0;

      if (busy_q) begin
        if (div_count_q == clkdiv_q) begin
          div_count_q <= 16'd0;
          sclk_q <= ~sclk_q;
          if (!sclk_q) begin
            rx_shift_q <= {rx_shift_q[5:0], spi_miso};
            if (bit_count_q == 3'd7)
              rx_data_q <= {rx_shift_q, spi_miso};
          end else begin
            tx_shift_q <= {tx_shift_q[6:0], 1'b0};
            if (bit_count_q == 3'd7) begin
              busy_q <= 1'b0;
              sclk_q <= 1'b0;
              rx_valid_q <= 1'b1;
            end else begin
              bit_count_q <= bit_count_q + 3'd1;
            end
          end
        end else begin
          div_count_q <= div_count_q + 16'd1;
        end
      end

      // I-port register writes, then D-port writes as the explicit priority.
      if (i_valid && !i_err && |i_wstrb) begin
        unique case (i_off)
          DATA:   tx_data_q <= i_wdata[7:0];
          CTRL: begin
            cs_n_q <= i_wdata[1];
            if (i_wdata[0]) begin
              tx_shift_q <= tx_data_q;
              rx_shift_q <= 7'b0;
              bit_count_q <= 3'd0;
              div_count_q <= 16'd0;
              sclk_q <= 1'b0;
              busy_q <= 1'b1;
              rx_valid_q <= 1'b0;
            end
          end
          CLKDIV: clkdiv_q <= i_wdata[15:0];
          default: ;
        endcase
      end
      if (d_valid && !d_err && |d_wstrb) begin
        unique case (d_off)
          DATA:   tx_data_q <= d_wdata[7:0];
          CTRL: begin
            cs_n_q <= d_wdata[1];
            if (d_wdata[0]) begin
              tx_shift_q <= tx_data_q;
              rx_shift_q <= 7'b0;
              bit_count_q <= 3'd0;
              div_count_q <= 16'd0;
              sclk_q <= 1'b0;
              busy_q <= 1'b1;
              rx_valid_q <= 1'b0;
            end
          end
          CLKDIV: clkdiv_q <= d_wdata[15:0];
          default: ;
        endcase
      end
    end
  end

  // Registers intentionally consume only their documented low fields.
  // verilator lint_off UNUSED
  wire unused_write_bits = ^{i_wdata[31:16], d_wdata[31:16]};
  // verilator lint_on UNUSED
endmodule
