module axuart_phy_loopback_top (
  input logic clk,
  input logic rst,
  input logic tx_valid,
  input logic [7:0] tx_data,
  output logic tx_ready,
  output logic rx_valid,
  output logic [7:0] rx_data,
  input logic rx_ready
);
  wire serial;
  axuart_phy #(.CLOCK_HZ(100), .BAUD(10)) dut (
    .clk(clk), .rst(rst), .tx_valid(tx_valid), .tx_data(tx_data),
    .tx_ready(tx_ready), .tx(serial), .rx(serial), .rx_valid(rx_valid),
    .rx_data(rx_data), .rx_ready(rx_ready)
  );
endmodule
