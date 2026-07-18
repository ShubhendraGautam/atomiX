// ULX3S-compatible 16-bit SDR SDRAM controller.
//
// The controller presents the existing independent I/D aXbus RAM ports to a
// single MT48LC16M16-style SDR SDRAM channel.  A request is split into two
// 16-bit transfers and completes only after the row has been precharged.  It
// deliberately uses a conservative, no-burst sequence at 25 MHz: functional
// bring-up and a stable bus contract come before bandwidth optimisation.
//
// Address layout for the 32 MiB x16 part:
//   byte[24:23] = bank, byte[22:10] = row, byte[9:1] = column.
module axsdram #(
  parameter [31:0] BASE = 32'h8000_0000,
  parameter integer BYTES = 32 * 1024 * 1024,
  parameter integer POWERUP_CYCLES = 5000,  // 200 us at 25 MHz
  parameter integer REFRESH_CYCLES = 195    // 7.8 us at 25 MHz
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

  output logic        init_done
);
  localparam integer POWERUP_WIDTH = $clog2(POWERUP_CYCLES + 1);
  localparam integer REFRESH_WIDTH = $clog2(REFRESH_CYCLES + 1);
  // verilator lint_off WIDTH
  localparam [POWERUP_WIDTH-1:0] POWERUP_LAST = POWERUP_CYCLES - 1;
  localparam [REFRESH_WIDTH-1:0] REFRESH_LAST = REFRESH_CYCLES - 1;
  // verilator lint_on WIDTH

  localparam logic [3:0] CMD_NOP       = 4'b0111;
  localparam logic [3:0] CMD_ACTIVE    = 4'b0011;
  localparam logic [3:0] CMD_READ      = 4'b0101;
  localparam logic [3:0] CMD_WRITE     = 4'b0100;
  localparam logic [3:0] CMD_PRECHARGE = 4'b0010;
  localparam logic [3:0] CMD_REFRESH   = 4'b0001;
  localparam logic [3:0] CMD_MRS       = 4'b0000;

  typedef enum logic [4:0] {
    ST_POWERUP,
    ST_INIT_PRECHARGE,
    ST_INIT_PRECHARGE_WAIT,
    ST_INIT_REFRESH_1,
    ST_INIT_REFRESH_1_WAIT,
    ST_INIT_REFRESH_2,
    ST_INIT_REFRESH_2_WAIT,
    ST_INIT_MRS,
    ST_INIT_MRS_WAIT,
    ST_READY,
    ST_REFRESH,
    ST_REFRESH_WAIT,
    ST_ACTIVE,
    ST_TRCD_WAIT,
    ST_READ_LO,
    ST_READ_HI,
    ST_READ_WAIT_LO,
    ST_WRITE_LO,
    ST_WRITE_HI,
    ST_WRITE_RECOVER_0,
    ST_WRITE_RECOVER_1,
    ST_PRECHARGE,
    ST_PRECHARGE_WAIT,
    ST_RESPONSE
  } state_t;

  state_t state;
  logic [POWERUP_WIDTH-1:0] powerup_count;
  logic [REFRESH_WIDTH-1:0] refresh_count;
  logic refresh_due;
  logic request_is_i;
  logic [31:0] request_addr, request_wdata;
  logic [3:0] request_wstrb;
  logic [15:0] read_lo, read_hi;

  wire i_ok = i_addr >= BASE && (i_addr - BASE) <= BYTES - 4 &&
              i_addr[1:0] == 2'b00;
  wire d_ok = d_addr >= BASE && (d_addr - BASE) <= BYTES - 4 &&
              d_addr[1:0] == 2'b00;
  // verilator lint_off WIDTH
  wire [23:0] request_word = (request_addr - BASE) >> 1;
  // verilator lint_on WIDTH
  wire [1:0] request_bank = request_word[23:22];
  wire [12:0] request_row = request_word[21:9];
  wire [8:0] request_column = request_word[8:0];
  wire request_write = |request_wstrb;

  logic [15:0] dq_out;
  logic dq_oe;
  wire [15:0] dq_in = sdram_dq_i;
  assign sdram_dq_o = dq_out;
  assign sdram_dq_oe = dq_oe;

  always_comb begin
    i_ready = 1'b0;
    i_rdata = {read_hi, read_lo};
    i_err = 1'b0;
    d_ready = 1'b0;
    d_rdata = {read_hi, read_lo};
    d_err = 1'b0;

    sdram_cke = state != ST_POWERUP;
    sdram_cs_n = CMD_NOP[3];
    sdram_ras_n = CMD_NOP[2];
    sdram_cas_n = CMD_NOP[1];
    sdram_we_n = CMD_NOP[0];
    sdram_ba = 2'b00;
    sdram_a = 13'b0;
    sdram_dqm = 2'b00;
    dq_out = 16'b0;
    dq_oe = 1'b0;

    // Decode faults complete immediately while the controller is otherwise
    // idle.  A valid RAM request is latched below and completes in ST_RESPONSE.
    if (state == ST_READY) begin
      if (i_valid && !i_ok) begin
        i_ready = 1'b1;
        i_err = 1'b1;
      end else if (!i_valid && d_valid && !d_ok) begin
        d_ready = 1'b1;
        d_err = 1'b1;
      end
    end else if (state == ST_RESPONSE) begin
      if (request_is_i) i_ready = i_valid;
      else d_ready = d_valid;
    end

    case (state)
      ST_INIT_PRECHARGE: begin
        {sdram_cs_n, sdram_ras_n, sdram_cas_n, sdram_we_n} = CMD_PRECHARGE;
        sdram_a[10] = 1'b1;  // all banks
      end
      ST_INIT_REFRESH_1, ST_INIT_REFRESH_2, ST_REFRESH:
        {sdram_cs_n, sdram_ras_n, sdram_cas_n, sdram_we_n} = CMD_REFRESH;
      ST_INIT_MRS: begin
        {sdram_cs_n, sdram_ras_n, sdram_cas_n, sdram_we_n} = CMD_MRS;
        // Burst length 1, sequential, CAS latency 2, standard operation.
        sdram_a = 13'b0000000100000;
      end
      ST_ACTIVE: begin
        {sdram_cs_n, sdram_ras_n, sdram_cas_n, sdram_we_n} = CMD_ACTIVE;
        sdram_ba = request_bank;
        sdram_a = request_row;
      end
      ST_READ_LO, ST_READ_HI: begin
        {sdram_cs_n, sdram_ras_n, sdram_cas_n, sdram_we_n} = CMD_READ;
        sdram_ba = request_bank;
        sdram_a[8:0] = request_column + {{8{1'b0}}, (state == ST_READ_HI)};
      end
      ST_WRITE_LO, ST_WRITE_HI: begin
        {sdram_cs_n, sdram_ras_n, sdram_cas_n, sdram_we_n} = CMD_WRITE;
        sdram_ba = request_bank;
        sdram_a[8:0] = request_column + {{8{1'b0}}, (state == ST_WRITE_HI)};
        sdram_dqm = state == ST_WRITE_HI ? ~request_wstrb[3:2] : ~request_wstrb[1:0];
        dq_out = state == ST_WRITE_HI ? request_wdata[31:16] : request_wdata[15:0];
        dq_oe = 1'b1;
      end
      ST_PRECHARGE: begin
        {sdram_cs_n, sdram_ras_n, sdram_cas_n, sdram_we_n} = CMD_PRECHARGE;
        sdram_ba = request_bank;
      end
      default: begin end
    endcase
  end

  always_ff @(posedge clk) begin
    if (rst) begin
      state <= ST_POWERUP;
      powerup_count <= '0;
      refresh_count <= '0;
      refresh_due <= 1'b0;
      request_is_i <= 1'b0;
      request_addr <= 32'b0;
      request_wdata <= 32'b0;
      request_wstrb <= 4'b0;
      read_lo <= 16'b0;
      read_hi <= 16'b0;
      init_done <= 1'b0;
    end else begin
      // Keep time while transactions are active.  The request path is short,
      // but refresh is a real-time SDRAM obligation, not an idle-cycle count.
      if (init_done && state != ST_REFRESH && !refresh_due) begin
        if (refresh_count == REFRESH_LAST) refresh_due <= 1'b1;
        else refresh_count <= refresh_count + 1'b1;
      end
      case (state)
        ST_POWERUP: begin
          if (powerup_count == POWERUP_LAST) state <= ST_INIT_PRECHARGE;
          else powerup_count <= powerup_count + 1'b1;
        end
        ST_INIT_PRECHARGE: state <= ST_INIT_PRECHARGE_WAIT;
        ST_INIT_PRECHARGE_WAIT: state <= ST_INIT_REFRESH_1;
        ST_INIT_REFRESH_1: state <= ST_INIT_REFRESH_1_WAIT;
        ST_INIT_REFRESH_1_WAIT: state <= ST_INIT_REFRESH_2;
        ST_INIT_REFRESH_2: state <= ST_INIT_REFRESH_2_WAIT;
        ST_INIT_REFRESH_2_WAIT: state <= ST_INIT_MRS;
        ST_INIT_MRS: state <= ST_INIT_MRS_WAIT;
        ST_INIT_MRS_WAIT: begin
          state <= ST_READY;
          init_done <= 1'b1;
          refresh_count <= '0;
          refresh_due <= 1'b0;
        end
        ST_READY: begin
          if (refresh_due) begin
            state <= ST_REFRESH;
          end else begin
            if (i_valid && i_ok) begin
              request_is_i <= 1'b1;
              request_addr <= i_addr;
              request_wdata <= i_wdata;
              request_wstrb <= i_wstrb;
              state <= ST_ACTIVE;
            end else if (!i_valid && d_valid && d_ok) begin
              request_is_i <= 1'b0;
              request_addr <= d_addr;
              request_wdata <= d_wdata;
              request_wstrb <= d_wstrb;
              state <= ST_ACTIVE;
            end
          end
        end
        ST_REFRESH: begin
          state <= ST_REFRESH_WAIT;
          refresh_count <= '0;
          refresh_due <= 1'b0;
        end
        ST_REFRESH_WAIT: state <= ST_READY;
        ST_ACTIVE: state <= ST_TRCD_WAIT;
        ST_TRCD_WAIT: state <= request_write ? ST_WRITE_LO : ST_READ_LO;
        ST_READ_LO: state <= ST_READ_HI;
        ST_READ_HI: begin
          read_lo <= dq_in;
          state <= ST_READ_WAIT_LO;
        end
        ST_READ_WAIT_LO: begin
          read_hi <= dq_in;
          state <= ST_PRECHARGE;
        end
        ST_WRITE_LO: state <= ST_WRITE_HI;
        ST_WRITE_HI: state <= ST_WRITE_RECOVER_0;
        ST_WRITE_RECOVER_0: state <= ST_WRITE_RECOVER_1;
        ST_WRITE_RECOVER_1: state <= ST_PRECHARGE;
        ST_PRECHARGE: state <= ST_PRECHARGE_WAIT;
        ST_PRECHARGE_WAIT: state <= ST_RESPONSE;
        ST_RESPONSE: begin
          if ((request_is_i && i_valid) || (!request_is_i && d_valid)) state <= ST_READY;
        end
        default: state <= ST_POWERUP;
      endcase
    end
  end
endmodule
