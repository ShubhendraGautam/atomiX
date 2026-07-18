// aX SoC v1 simulation/synthesis top level.  The core's Harvard ports are
// independently decoded onto dual-port BRAM/peripheral implementations.
module soc_top #(
  parameter logic [31:0] RESET_PC = 32'h0000_1000,
  parameter int unsigned RAM_BYTES = 128 * 1024,
  parameter string ROM_INIT_FILE = "",
  parameter string RAM_INIT_FILE = ""
) (
  input  logic       clk,
  input  logic       rst,
  input  logic       irq_external,
  output logic       uart_tx_valid,
  output logic [7:0] uart_tx_data,
  output logic       finished,
  output logic [15:0] exit_code
);
  logic ibus_valid, ibus_ready, ibus_err;
  logic [31:0] ibus_addr, ibus_rdata, ibus_wdata;
  logic [3:0] ibus_wstrb;
  logic dbus_valid, dbus_ready, dbus_err;
  logic [31:0] dbus_addr, dbus_wdata, dbus_rdata;
  logic [3:0] dbus_wstrb;

  logic i_rom_valid, i_ram_valid, i_test_valid, i_clint_valid, i_uart_valid;
  logic d_rom_valid, d_ram_valid, d_test_valid, d_clint_valid, d_uart_valid;
  logic i_rom_ready, i_rom_err, i_ram_ready, i_ram_err, i_test_ready, i_test_err;
  logic i_clint_ready, i_clint_err, i_uart_ready, i_uart_err;
  logic d_rom_ready, d_rom_err, d_ram_ready, d_ram_err, d_test_ready, d_test_err;
  logic d_clint_ready, d_clint_err, d_uart_ready, d_uart_err;
  logic [31:0] i_rom_rdata, i_ram_rdata, i_test_rdata, i_clint_rdata, i_uart_rdata;
  logic [31:0] d_rom_rdata, d_ram_rdata, d_test_rdata, d_clint_rdata, d_uart_rdata;
  logic irq_software, irq_timer;

  // The standalone SoC does not consume debug/RVFI observability signals.
  // verilator lint_off PINCONNECTEMPTY
  axcore #(.RESET_PC(RESET_PC)) u_core (
    .clk(clk), .rst(rst),
    .ibus_valid(ibus_valid), .ibus_addr(ibus_addr), .ibus_wdata(ibus_wdata),
    .ibus_wstrb(ibus_wstrb), .ibus_ready(ibus_ready),
    .ibus_rdata(ibus_rdata), .ibus_err(ibus_err),
    .dbus_valid(dbus_valid), .dbus_addr(dbus_addr), .dbus_wdata(dbus_wdata),
    .dbus_wstrb(dbus_wstrb), .dbus_ready(dbus_ready), .dbus_rdata(dbus_rdata),
    .dbus_err(dbus_err),
    .irq_software(irq_software), .irq_timer(irq_timer),
    .irq_external(irq_external),
    .trace_valid(), .trace_trap(), .trace_pc(), .trace_insn(),
    .trace_cause(), .trace_tval(), .trace_rd_we(), .trace_rd(),
    .trace_rd_val(), .trace_mstatus(), .trace_mtvec(), .trace_mepc(),
    .trace_mcause(), .trace_mtval(), .trace_mscratch(), .trace_mie(),
    .trace_mip(), .trace_prv(),
    .rvfi_valid(), .rvfi_order(), .rvfi_insn(), .rvfi_trap(), .rvfi_halt(),
    .rvfi_intr(), .rvfi_mode(), .rvfi_ixl(), .rvfi_rs1_addr(),
    .rvfi_rs2_addr(), .rvfi_rs1_rdata(), .rvfi_rs2_rdata(), .rvfi_rd_addr(),
    .rvfi_rd_wdata(), .rvfi_pc_rdata(), .rvfi_pc_wdata(), .rvfi_mem_addr(),
    .rvfi_mem_rmask(), .rvfi_mem_wmask(), .rvfi_mem_rdata(), .rvfi_mem_wdata()
  );
  // verilator lint_on PINCONNECTEMPTY

  axbus_mux #(.RAM_SIZE(RAM_BYTES)) u_ibus_mux (
    .m_valid(ibus_valid), .m_addr(ibus_addr), .m_ready(ibus_ready),
    .m_rdata(ibus_rdata), .m_err(ibus_err),
    .rom_valid(i_rom_valid), .rom_ready(i_rom_ready), .rom_rdata(i_rom_rdata), .rom_err(i_rom_err),
    .ram_valid(i_ram_valid), .ram_ready(i_ram_ready), .ram_rdata(i_ram_rdata), .ram_err(i_ram_err),
    .test_valid(i_test_valid), .test_ready(i_test_ready), .test_rdata(i_test_rdata), .test_err(i_test_err),
    .clint_valid(i_clint_valid), .clint_ready(i_clint_ready), .clint_rdata(i_clint_rdata), .clint_err(i_clint_err),
    .uart_valid(i_uart_valid), .uart_ready(i_uart_ready), .uart_rdata(i_uart_rdata), .uart_err(i_uart_err)
  );

  axbus_mux #(.RAM_SIZE(RAM_BYTES)) u_dbus_mux (
    .m_valid(dbus_valid), .m_addr(dbus_addr), .m_ready(dbus_ready),
    .m_rdata(dbus_rdata), .m_err(dbus_err),
    .rom_valid(d_rom_valid), .rom_ready(d_rom_ready), .rom_rdata(d_rom_rdata), .rom_err(d_rom_err),
    .ram_valid(d_ram_valid), .ram_ready(d_ram_ready), .ram_rdata(d_ram_rdata), .ram_err(d_ram_err),
    .test_valid(d_test_valid), .test_ready(d_test_ready), .test_rdata(d_test_rdata), .test_err(d_test_err),
    .clint_valid(d_clint_valid), .clint_ready(d_clint_ready), .clint_rdata(d_clint_rdata), .clint_err(d_clint_err),
    .uart_valid(d_uart_valid), .uart_ready(d_uart_ready), .uart_rdata(d_uart_rdata), .uart_err(d_uart_err)
  );

  axrom #(.INIT_FILE(ROM_INIT_FILE)) u_rom (
    .clk(clk), .i_valid(i_rom_valid), .i_addr(ibus_addr), .i_wdata(32'b0),
    .i_wstrb(4'b0), .i_ready(i_rom_ready), .i_rdata(i_rom_rdata), .i_err(i_rom_err),
    .d_valid(d_rom_valid), .d_addr(dbus_addr), .d_wdata(dbus_wdata),
    .d_wstrb(dbus_wstrb), .d_ready(d_rom_ready), .d_rdata(d_rom_rdata), .d_err(d_rom_err)
  );

  // The fetch-side page-table walker writes PTE A-bits through the I port.
  axram #(.BYTES(RAM_BYTES), .INIT_FILE(RAM_INIT_FILE)) u_ram (
    .clk(clk), .rst(rst), .i_valid(i_ram_valid), .i_addr(ibus_addr), .i_wdata(ibus_wdata),
    .i_wstrb(ibus_wstrb), .i_ready(i_ram_ready), .i_rdata(i_ram_rdata), .i_err(i_ram_err),
    .d_valid(d_ram_valid), .d_addr(dbus_addr), .d_wdata(dbus_wdata),
    .d_wstrb(dbus_wstrb), .d_ready(d_ram_ready), .d_rdata(d_ram_rdata), .d_err(d_ram_err)
  );

  test_finisher u_test (
    .clk(clk), .rst(rst), .i_valid(i_test_valid), .i_wdata(32'b0), .i_wstrb(4'b0),
    .i_ready(i_test_ready), .i_rdata(i_test_rdata), .i_err(i_test_err),
    .d_valid(d_test_valid), .d_wdata(dbus_wdata), .d_wstrb(dbus_wstrb),
    .d_ready(d_test_ready), .d_rdata(d_test_rdata), .d_err(d_test_err),
    .finished(finished), .exit_code(exit_code)
  );

  clint u_clint (
    .clk(clk), .rst(rst), .i_valid(i_clint_valid), .i_addr(ibus_addr), .i_wdata(32'b0),
    .i_wstrb(4'b0), .i_ready(i_clint_ready), .i_rdata(i_clint_rdata), .i_err(i_clint_err),
    .d_valid(d_clint_valid), .d_addr(dbus_addr), .d_wdata(dbus_wdata), .d_wstrb(dbus_wstrb),
    .d_ready(d_clint_ready), .d_rdata(d_clint_rdata), .d_err(d_clint_err),
    .irq_software(irq_software), .irq_timer(irq_timer)
  );

  uart u_uart (
    .clk(clk), .rst(rst), .i_valid(i_uart_valid), .i_addr(ibus_addr), .i_wdata(32'b0),
    .i_wstrb(4'b0), .i_ready(i_uart_ready), .i_rdata(i_uart_rdata), .i_err(i_uart_err),
    .d_valid(d_uart_valid), .d_addr(dbus_addr), .d_wdata(dbus_wdata), .d_wstrb(dbus_wstrb),
    .d_ready(d_uart_ready), .d_rdata(d_uart_rdata), .d_err(d_uart_err),
    .tx_valid(uart_tx_valid), .tx_data(uart_tx_data)
  );
endmodule
