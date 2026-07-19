// TPU-lite role: an int8 weight-stationary systolic GEMM accelerator.
//
// The role computes C = acc_base + A x W over signed 8-bit operands with
// 32-bit accumulation:
//
//   A is M x 8 activations (M up to 256 rows), W is the stationary 8 x 8
//   weight tile, C is M x 8 signed 32-bit results.  CTRL.ACC accumulates
//   into the existing C contents (how software tiles GEMMs with K > 8:
//   load the next weight/activation slice and ring the doorbell again);
//   CTRL.RELU clamps negative results to zero on the way out.
//
// The engine is an 8 x 8 grid of MACs with the weight held stationary in
// each cell.  Partial sums flow down the columns through pipeline
// registers, one array row per advance; the activation element for array
// row r passes through an r-deep skew delay line so the psum wave for
// activation row m meets a[m][r] exactly at row r.  All eight columns of
// row m therefore finish together at the array bottom, where the drain
// sequencer writes (or read-modify-writes, under ACC) them into C.  A job
// over M rows takes M + 7 array advances plus the load/drain cycles.
//
// Window layout (common role header per DESIGN.md section 3.3 first):
//
//   0x0000  ROLE_ID   RO  "TPUL"
//   0x0004  VERSION   RO  role programming-model revision
//   0x0008  DOORBELL  WO  any write starts a job when idle
//   0x000c  STATUS    R/W1C  bit0 BUSY, bit1 DONE (write 1 to clear DONE)
//   0x0010  CTRL      bit0 RELU, bit1 ACC; latched at the doorbell
//   0x0014  M         activation rows for the next job; 0 completes
//                     immediately, values above 256 clamp to 256
//   0x0018  COUNT     RO  completed-job counter
//   0x0100  weight tile, 16 words: row r in words 2r and 2r+1, one int8
//           per byte, w[r][c] in byte c%4 of word 2r + c/4 (little-endian)
//   0x1000  A buffer, 512 words: row m in words 2m and 2m+1, packed like W
//   0x2000  C buffer, 2048 words: c[m][col] at word 8m + col
//
// The buffers keep the loopback role's block-RAM discipline: two
// synchronous ports each (data-port MMIO and the engine), MMIO reads
// complete with one aXbus wait state, MMIO writes are full-word only
// (partial strobes error).  The weight tile and buffers are data-port
// only; the fetch port sees just the header registers.  CTRL, M, and the
// weight tile are writable only while idle, and software must not touch
// the buffers while BUSY.
module axrole #(
  parameter logic [31:0] BASE = 32'h4000_0000
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
  output logic        d_err
);
  localparam logic [31:0] ROLE_ID      = 32'h5450_554c;  // "TPUL"
  localparam logic [31:0] ROLE_VERSION = 32'h0000_0001;
  localparam logic [15:0] OFF_ID       = 16'h0000;
  localparam logic [15:0] OFF_VERSION  = 16'h0004;
  localparam logic [15:0] OFF_DOORBELL = 16'h0008;
  localparam logic [15:0] OFF_STATUS   = 16'h000c;
  localparam logic [15:0] OFF_CTRL     = 16'h0010;
  localparam logic [15:0] OFF_M        = 16'h0014;
  localparam logic [15:0] OFF_COUNT    = 16'h0018;
  localparam logic [15:0] W_BASE       = 16'h0100;
  localparam logic [15:0] A_BASE       = 16'h1000;
  localparam logic [15:0] C_BASE       = 16'h2000;

  // Engine sequencer: per array step, load one activation row (three
  // cycles of synchronous A reads), advance the array once, then drain the
  // completed output row bottom-of-column by column.
  localparam logic [2:0] E_IDLE     = 3'd0;
  localparam logic [2:0] E_LOAD0    = 3'd1;
  localparam logic [2:0] E_LOAD1    = 3'd2;
  localparam logic [2:0] E_LOAD2    = 3'd3;
  localparam logic [2:0] E_ADV      = 3'd4;
  localparam logic [2:0] E_DRAIN_RD = 3'd5;
  localparam logic [2:0] E_DRAIN_WR = 3'd6;

  logic [31:0] wreg_q [0:15];
  logic [31:0] abuf [0:511];
  logic [31:0] cbuf [0:2047];

  logic [31:0] ctrl_q, m_q, count_q;
  logic        busy_q, done_q;
  logic [8:0]  job_m_q;
  logic        job_relu_q, job_acc_q;
  logic [2:0]  state_q;
  logic [8:0]  step_q;
  logic [2:0]  col_q;
  logic [63:0] row_in_q;
  logic [31:0] a_mmio_rdata_q, c_mmio_rdata_q;
  logic [31:0] a_eng_rdata_q, c_eng_rdata_q;
  logic        buf_pending_q;

  wire i_in_range = i_addr >= BASE && i_addr - BASE < 32'h0001_0000;
  wire d_in_range = d_addr >= BASE && d_addr - BASE < 32'h0001_0000;
  wire [15:0] i_off = i_addr[15:0];
  wire [15:0] d_off = d_addr[15:0];

  function automatic logic reg_offset(input logic [15:0] off);
    reg_offset = off == OFF_ID || off == OFF_VERSION ||
                 off == OFF_DOORBELL || off == OFF_STATUS ||
                 off == OFF_CTRL || off == OFF_M || off == OFF_COUNT;
  endfunction
  function automatic logic wreg_offset(input logic [15:0] off);
    wreg_offset = off >= W_BASE && off < W_BASE + 16'h0040;
  endfunction
  function automatic logic abuf_offset(input logic [15:0] off);
    abuf_offset = off >= A_BASE && off < A_BASE + 16'h0800;
  endfunction
  function automatic logic cbuf_offset(input logic [15:0] off);
    cbuf_offset = off >= C_BASE && off < C_BASE + 16'h2000;
  endfunction
  function automatic logic [31:0] read_reg(input logic [15:0] off);
    unique case (off)
      OFF_ID:      read_reg = ROLE_ID;
      OFF_VERSION: read_reg = ROLE_VERSION;
      OFF_STATUS:  read_reg = {30'b0, done_q, busy_q};
      OFF_CTRL:    read_reg = ctrl_q;
      OFF_M:       read_reg = m_q;
      OFF_COUNT:   read_reg = count_q;
      default:     read_reg = 32'b0;
    endcase
  endfunction
  function automatic logic [31:0] merge_bytes(
      input logic [31:0] old_value, input logic [31:0] new_value,
      input logic [3:0] strb);
    merge_bytes = old_value;
    if (strb[0]) merge_bytes[7:0]   = new_value[7:0];
    if (strb[1]) merge_bytes[15:8]  = new_value[15:8];
    if (strb[2]) merge_bytes[23:16] = new_value[23:16];
    if (strb[3]) merge_bytes[31:24] = new_value[31:24];
  endfunction

  wire d_abuf_hit  = d_valid && d_in_range && abuf_offset(d_off) &&
                     d_addr[1:0] == 2'b00;
  wire d_cbuf_hit  = d_valid && d_in_range && cbuf_offset(d_off) &&
                     d_addr[1:0] == 2'b00;
  wire d_buf_hit   = d_abuf_hit || d_cbuf_hit;
  wire d_buf_write = d_buf_hit && d_wstrb == 4'hf;
  wire d_buf_read  = d_buf_hit && d_wstrb == 4'b0;
  wire d_wreg_hit  = d_valid && d_in_range && wreg_offset(d_off) &&
                     d_addr[1:0] == 2'b00;

  always_comb begin
    i_ready = i_valid;
    i_err   = i_valid && (!i_in_range || !reg_offset(i_off) || i_addr[1:0] != 2'b00);
    i_rdata = read_reg(i_off);
    d_ready = 1'b0;
    d_rdata = 32'b0;
    d_err   = 1'b0;
    if (d_valid) begin
      if (d_buf_write) begin
        d_ready = 1'b1;
      end else if (d_buf_read) begin
        // Synchronous block-RAM read: one wait state, data registered.
        d_ready = buf_pending_q;
        d_rdata = d_abuf_hit ? a_mmio_rdata_q : c_mmio_rdata_q;
      end else if (d_wreg_hit) begin
        d_ready = 1'b1;
        d_rdata = wreg_q[d_off[5:2]];
      end else if (d_in_range && reg_offset(d_off) && d_addr[1:0] == 2'b00) begin
        d_ready = 1'b1;
        d_rdata = read_reg(d_off);
      end else begin
        d_ready = 1'b1;
        d_err   = 1'b1;
      end
    end
  end

  // Weight-stationary systolic datapath.  a_feed[r] is the activation
  // entering array row r this advance: row 0 straight from the assembled
  // row register, row r through r skew stages, so the psum wave for
  // activation row m sees a[m][r] exactly when it reaches array row r.
  wire advance = busy_q && state_q == E_ADV;

  logic [7:0] dly_q [1:7][0:6];
  always_ff @(posedge clk) begin
    if (advance) begin
      for (int r = 1; r < 8; r++) begin
        dly_q[r][0] <= row_in_q[8*r +: 8];
        for (int j = 1; j < 7; j++) dly_q[r][j] <= dly_q[r][j-1];
      end
    end
  end

  wire [7:0] a_feed [0:7];
  assign a_feed[0] = row_in_q[7:0];
  generate
    for (genvar r = 1; r < 8; r++) begin : g_skew
      assign a_feed[r] = dly_q[r][r-1];
    end
  endgenerate

  wire [7:0] w_val [0:7][0:7];
  wire [15:0] prod [0:7][0:7];
  logic [31:0] p_q [0:7][0:7];
  wire [31:0] psum_in [0:7][0:7];
  generate
    for (genvar r = 0; r < 8; r++) begin : g_row
      for (genvar c = 0; c < 8; c++) begin : g_col
        assign w_val[r][c] = wreg_q[2*r + c/4][8*(c%4) +: 8];
        assign prod[r][c] = $signed(a_feed[r]) * $signed(w_val[r][c]);
        if (r == 0) assign psum_in[r][c] = 32'b0;
        else assign psum_in[r][c] = p_q[r-1][c];
      end
    end
  endgenerate

  always_ff @(posedge clk) begin
    if (advance) begin
      for (int r = 0; r < 8; r++)
        for (int c = 0; c < 8; c++)
          p_q[r][c] <= psum_in[r][c] + {{16{prod[r][c][15]}}, prod[r][c]};
    end
  end

  // Drain datapath: after the advance of step s the bottom psum registers
  // hold output row s - 7 (modulo-256 subtraction is exact here because
  // the drained row index never exceeds 255).
  wire [7:0]  drain_m = step_q[7:0] - 8'd7;
  wire [10:0] c_eng_idx = {drain_m, col_q};
  wire        c_eng_write = busy_q && state_q == E_DRAIN_WR;
  wire [31:0] drain_sum = (job_acc_q ? c_eng_rdata_q : 32'b0) + p_q[7][col_q];
  wire [31:0] c_eng_wdata = (job_relu_q && drain_sum[31]) ? 32'b0 : drain_sum;
  wire [8:0]  a_eng_addr = {step_q[7:0], state_q == E_LOAD1};

  // A buffer port A: data-port MMIO.  A separate always_ff per port is the
  // true-dual-port block-RAM inference pattern.
  always_ff @(posedge clk) begin
    if (d_abuf_hit && d_wstrb == 4'hf) abuf[d_off[10:2]] <= d_wdata;
    else if (d_abuf_hit && d_wstrb == 4'b0) a_mmio_rdata_q <= abuf[d_off[10:2]];
  end
  // A buffer port B: the engine's activation-row reads.
  always_ff @(posedge clk) begin
    a_eng_rdata_q <= abuf[a_eng_addr];
  end

  // C buffer port A: data-port MMIO.
  always_ff @(posedge clk) begin
    if (d_cbuf_hit && d_wstrb == 4'hf) cbuf[d_off[12:2]] <= d_wdata;
    else if (d_cbuf_hit && d_wstrb == 4'b0) c_mmio_rdata_q <= cbuf[d_off[12:2]];
  end
  // C buffer port B: the drain sequencer (read phase only under ACC).
  always_ff @(posedge clk) begin
    if (c_eng_write) cbuf[c_eng_idx] <= c_eng_wdata;
    else c_eng_rdata_q <= cbuf[c_eng_idx];
  end

  // The weight tile is a small register file with one write port; writes
  // while BUSY are ignored so the stationary operand cannot change mid-job.
  always_ff @(posedge clk) begin
    if (d_wreg_hit && |d_wstrb && !busy_q)
      wreg_q[d_off[5:2]] <= merge_bytes(wreg_q[d_off[5:2]], d_wdata, d_wstrb);
  end

  task automatic apply_reg_write(input logic [15:0] off,
                                 input logic [31:0] wdata,
                                 input logic [3:0] strb);
    unique case (off)
      OFF_DOORBELL: begin
        if (!busy_q) begin
          done_q <= 1'b0;
          if (m_q == 32'b0) begin
            done_q  <= 1'b1;
            count_q <= count_q + 32'd1;
          end else begin
            busy_q     <= 1'b1;
            job_m_q    <= (m_q > 32'd256) ? 9'd256 : m_q[8:0];
            job_relu_q <= ctrl_q[0];
            job_acc_q  <= ctrl_q[1];
          end
        end
      end
      OFF_STATUS: begin
        // DONE is write-1-to-clear; its bit lives in byte lane 0.
        if (strb[0] && wdata[1]) done_q <= 1'b0;
      end
      OFF_CTRL: if (!busy_q) ctrl_q <= merge_bytes(ctrl_q, wdata, strb);
      OFF_M:    if (!busy_q) m_q <= merge_bytes(m_q, wdata, strb);
      default: ;
    endcase
  endtask

  // Advance to array step step_q + 1, or finish the job after the last
  // drain.  Steps at or past job_m stream zero rows to flush the array.
  task automatic advance_step();
    if (step_q == job_m_q + 9'd6) begin
      busy_q  <= 1'b0;
      done_q  <= 1'b1;
      count_q <= count_q + 32'd1;
      state_q <= E_IDLE;
    end else begin
      step_q <= step_q + 9'd1;
      if (step_q + 9'd1 < job_m_q) state_q <= E_LOAD0;
      else begin
        row_in_q <= 64'b0;
        state_q  <= E_ADV;
      end
    end
  endtask

  always_ff @(posedge clk) begin
    if (rst) begin
      ctrl_q        <= 32'b0;
      m_q           <= 32'b0;
      count_q       <= 32'b0;
      busy_q        <= 1'b0;
      done_q        <= 1'b0;
      job_m_q       <= 9'b0;
      job_relu_q    <= 1'b0;
      job_acc_q     <= 1'b0;
      state_q       <= E_IDLE;
      step_q        <= 9'b0;
      col_q         <= 3'b0;
      row_in_q      <= 64'b0;
      buf_pending_q <= 1'b0;
    end else begin
      buf_pending_q <= d_buf_read && !buf_pending_q;
      // Same conflict rule as the reference CLINT: if both ports write a
      // register in one cycle the D port wins.
      if (i_valid && !i_err && |i_wstrb) apply_reg_write(i_off, i_wdata, i_wstrb);
      if (d_valid && !d_err && |d_wstrb && !d_buf_hit && !d_wreg_hit)
        apply_reg_write(d_off, d_wdata, d_wstrb);
      if (busy_q) begin
        unique case (state_q)
          E_IDLE: begin
            step_q  <= 9'd0;
            state_q <= E_LOAD0;
          end
          E_LOAD0: state_q <= E_LOAD1;
          E_LOAD1: begin
            row_in_q[31:0] <= a_eng_rdata_q;
            state_q        <= E_LOAD2;
          end
          E_LOAD2: begin
            row_in_q[63:32] <= a_eng_rdata_q;
            state_q         <= E_ADV;
          end
          E_ADV: begin
            if (step_q >= 9'd7) begin
              col_q   <= 3'd0;
              state_q <= job_acc_q ? E_DRAIN_RD : E_DRAIN_WR;
            end else advance_step();
          end
          E_DRAIN_RD: state_q <= E_DRAIN_WR;
          E_DRAIN_WR: begin
            if (col_q == 3'd7) advance_step();
            else begin
              col_q   <= col_q + 3'd1;
              state_q <= job_acc_q ? E_DRAIN_RD : E_DRAIN_WR;
            end
          end
          default: state_q <= E_IDLE;
        endcase
      end
    end
  end
endmodule
