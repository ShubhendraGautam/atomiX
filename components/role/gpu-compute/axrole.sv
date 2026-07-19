// GPU-compute role: an 8-lane SIMT data-parallel vector engine.
//
// The second real accelerator behind the shell role window (DESIGN.md §3.3).
// Where TPU-lite is a fixed-function systolic GEMM, this role is programmable:
// software uploads a short straight-line kernel and a flat global data buffer,
// sets the thread count, and rings the doorbell.  The engine then runs the
// kernel across all threads the way a GPU does — Single Instruction, Multiple
// Threads: 8 lanes execute the same instruction stream in lockstep, each on
// its own thread index, over ceil(NTHREADS / 8) waves.  It shares the exact
// doorbell/status/descriptor driver model the loopback and TPU roles proved.
//
// Window layout (common role header per DESIGN.md §3.3 first):
//
//   0x0000  ROLE_ID   RO  "GPUC"
//   0x0004  VERSION   RO  role programming-model revision
//   0x0008  DOORBELL  WO  any write starts a job when idle
//   0x000c  STATUS    R/W1C  bit0 BUSY, bit1 DONE (write 1 to clear DONE)
//   0x0010  NTHREADS  R/W  thread count for the next job; latched at doorbell,
//                     clamped to 8*4096.  Launches ceil(NTHREADS/8) waves;
//                     0 completes immediately.
//   0x0014  NINSN     R/W  kernel length (instructions); clamps to PROG_WORDS
//   0x0018  COUNT     RO   completed-job counter
//   0x0100  program memory, PROG_WORDS instruction words (see ISA below)
//   0x1000  global data buffer, DATA_WORDS 32-bit words, word-addressed;
//           kernel loads/stores index it (low bits, so it wraps like the
//           loopback buffer).  Software lays its input and output arrays out
//           here at chosen base offsets.
//
// SIMT execution model.  A job runs waves 0..ceil(NTHREADS/8)-1.  In wave w,
// lane L runs thread tid = 8*w + L; a lane whose tid >= NTHREADS is *inactive*
// (predicated off) — the classic SIMT tail: its stores are suppressed and its
// loads return zero, but it still steps the shared program counter in lockstep.
// The kernel is straight-line (no branches), so all lanes share one PC and
// never diverge; per-instruction global-store order is (wave, instruction,
// ascending lane), which the on-core reference in gpu.c reproduces exactly.
//
// Instruction encoding (32-bit): op[31:26] rd[25:23] ra[22:20] rb[19:17]
// imm[16:0] (sign-extended).  ALU-class instructions retire one per cycle
// across all 8 lanes; memory instructions serialize the 8 lanes onto the
// single engine buffer port (2 cycles/lane for a load, 1 for a store), which
// is how memory divergence costs cycles on real hardware.  Per-lane registers
// r0..r7 are flip-flops (x0 is a normal register here, not hardwired).
//
//   op  mnemonic         effect (per active lane)
//   0   HALT             end the wave early
//   1   TID   rd         rd = tid
//   2   LI    rd,imm     rd = imm
//   3   MOV   rd,ra      rd = ra
//   4   LDX   rd,ra      rd = gmem[ra]            (inactive lane -> rd = 0)
//   5   STX   ra,rb      gmem[ra] = rb            (inactive lane: suppressed)
//   6   ADD   rd,ra,rb   rd = ra + rb
//   7   SUB   rd,ra,rb   rd = ra - rb
//   8   MUL   rd,ra,rb   rd = (ra * rb)[31:0]     (signed, low word)
//   9   AND   rd,ra,rb   rd = ra & rb
//   10  OR    rd,ra,rb   rd = ra | rb
//   11  XOR   rd,ra,rb   rd = ra ^ rb
//   12  SLL   rd,ra,rb   rd = ra << rb[4:0]
//   13  SRL   rd,ra,rb   rd = ra >> rb[4:0]       (logical)
//   14  SRA   rd,ra,rb   rd = ra >>> rb[4:0]      (arithmetic)
//   15  MIN   rd,ra,rb   rd = signed_min(ra, rb)
//   16  MAX   rd,ra,rb   rd = signed_max(ra, rb)  (ReLU = MAX(x, zero-reg))
//   17  ADDI  rd,ra,imm  rd = ra + imm
//   18  MULI  rd,ra,imm  rd = (ra * imm)[31:0]
//
// The program memory and data buffer keep the block-RAM discipline of the
// other roles: two synchronous ports each (data-port MMIO and the engine),
// full-word MMIO writes only (partial strobes error), one aXbus wait state on
// reads, and the fetch port sees only the register page.  NTHREADS, NINSN, the
// program, and the buffer are writable only while idle, and software must not
// touch them while BUSY.
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
  localparam logic [31:0] ROLE_ID      = 32'h4750_5543;  // "GPUC"
  localparam logic [31:0] ROLE_VERSION = 32'h0000_0001;
  localparam logic [15:0] OFF_ID       = 16'h0000;
  localparam logic [15:0] OFF_VERSION  = 16'h0004;
  localparam logic [15:0] OFF_DOORBELL = 16'h0008;
  localparam logic [15:0] OFF_STATUS   = 16'h000c;
  localparam logic [15:0] OFF_NTHREADS = 16'h0010;
  localparam logic [15:0] OFF_NINSN    = 16'h0014;
  localparam logic [15:0] OFF_COUNT    = 16'h0018;
  localparam logic [15:0] PROG_BASE    = 16'h0100;
  localparam logic [15:0] DATA_BASE    = 16'h1000;
  localparam int unsigned PROG_WORDS   = 64;
  localparam int unsigned DATA_WORDS   = 4096;

  // Instruction opcodes.
  localparam logic [5:0] OP_HALT = 6'd0,  OP_TID = 6'd1,  OP_LI  = 6'd2,
                         OP_MOV  = 6'd3,  OP_LDX = 6'd4,  OP_STX = 6'd5,
                         OP_ADD  = 6'd6,  OP_SUB = 6'd7,  OP_MUL = 6'd8,
                         OP_AND  = 6'd9,  OP_OR  = 6'd10, OP_XOR = 6'd11,
                         OP_SLL  = 6'd12, OP_SRL = 6'd13, OP_SRA = 6'd14,
                         OP_MIN  = 6'd15, OP_MAX = 6'd16, OP_ADDI= 6'd17,
                         OP_MULI = 6'd18;

  // Engine sequencer states.
  localparam logic [2:0] E_IDLE  = 3'd0, E_FETCH = 3'd1, E_DECODE = 3'd2,
                         E_EXEC  = 3'd3, E_LDX_I = 3'd4, E_LDX_C  = 3'd5,
                         E_STX   = 3'd6, E_WAVE  = 3'd7;

  logic [31:0] prog [0:PROG_WORDS-1];
  logic [31:0] gmem [0:DATA_WORDS-1];
  logic [31:0] regs [0:7][0:7];  // [lane][reg]

  logic [31:0] nthreads_q, ninsn_q, count_q;
  logic        busy_q, done_q;
  logic [2:0]  state_q;
  logic [15:0] job_threads_q;   // clamped thread count for the running job
  logic [6:0]  job_ninsn_q;     // clamped kernel length for the running job
  logic [15:0] tid_base_q;      // 8*wave
  logic [6:0]  pc_q;
  logic [2:0]  mem_lane_q;

  logic [31:0] prog_mmio_rdata_q, data_mmio_rdata_q;
  logic [31:0] prog_eng_rdata_q, data_eng_rdata_q;
  logic        buf_pending_q;
  logic [31:0] insn_q;

  wire i_in_range = i_addr >= BASE && i_addr - BASE < 32'h0001_0000;
  wire d_in_range = d_addr >= BASE && d_addr - BASE < 32'h0001_0000;
  wire [15:0] i_off = i_addr[15:0];
  wire [15:0] d_off = d_addr[15:0];

  function automatic logic reg_offset(input logic [15:0] off);
    reg_offset = off == OFF_ID || off == OFF_VERSION ||
                 off == OFF_DOORBELL || off == OFF_STATUS ||
                 off == OFF_NTHREADS || off == OFF_NINSN || off == OFF_COUNT;
  endfunction
  function automatic logic prog_offset(input logic [15:0] off);
    prog_offset = off >= PROG_BASE && off < PROG_BASE + 16'(4 * PROG_WORDS);
  endfunction
  function automatic logic data_offset(input logic [15:0] off);
    data_offset = off >= DATA_BASE && off < DATA_BASE + 16'(4 * DATA_WORDS);
  endfunction
  function automatic logic [31:0] read_reg(input logic [15:0] off);
    unique case (off)
      OFF_ID:       read_reg = ROLE_ID;
      OFF_VERSION:  read_reg = ROLE_VERSION;
      OFF_STATUS:   read_reg = {30'b0, done_q, busy_q};
      OFF_NTHREADS: read_reg = nthreads_q;
      OFF_NINSN:    read_reg = ninsn_q;
      OFF_COUNT:    read_reg = count_q;
      default:      read_reg = 32'b0;
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

  // Program and data buffers are reachable through the data port only; the
  // fetch port sees just the register page.
  wire d_prog_hit   = d_valid && d_in_range && prog_offset(d_off) &&
                      d_addr[1:0] == 2'b00;
  wire d_data_hit   = d_valid && d_in_range && data_offset(d_off) &&
                      d_addr[1:0] == 2'b00;
  wire d_buf_hit    = d_prog_hit || d_data_hit;
  wire d_buf_read   = d_buf_hit && d_wstrb == 4'b0;
  // Index by explicit base subtraction: the data region (16 KiB at 0x1000) is
  // not aligned to its own size, so slicing address bits — the trick the 4 KiB
  // loopback buffer can use — would misindex it.
  wire [5:0]  d_prog_idx  = 6'(((d_off - PROG_BASE) >> 2));
  wire [11:0] d_data_idx  = 12'(((d_off - DATA_BASE) >> 2));

  always_comb begin
    i_ready = i_valid;
    i_err   = i_valid && (!i_in_range || !reg_offset(i_off) || i_addr[1:0] != 2'b00);
    i_rdata = read_reg(i_off);
    d_ready = 1'b0;
    d_rdata = 32'b0;
    d_err   = 1'b0;
    if (d_valid) begin
      if (d_buf_hit && d_wstrb == 4'hf) begin
        // Writes while BUSY are dropped rather than faulting, matching the
        // "do not touch while busy" contract of the other roles.
        d_ready = 1'b1;
      end else if (d_buf_read) begin
        // Synchronous block-RAM read: one wait state, data registered.
        d_ready = buf_pending_q;
        d_rdata = d_prog_hit ? prog_mmio_rdata_q : data_mmio_rdata_q;
      end else if (d_in_range && reg_offset(d_off) && d_addr[1:0] == 2'b00) begin
        d_ready = 1'b1;
        d_rdata = read_reg(d_off);
      end else begin
        d_ready = 1'b1;
        d_err   = 1'b1;
      end
    end
  end

  // Decode of the currently fetched instruction.
  wire [5:0]  op  = insn_q[31:26];
  wire [2:0]  rd  = insn_q[25:23];
  wire [2:0]  ra  = insn_q[22:20];
  wire [2:0]  rb  = insn_q[19:17];
  wire [31:0] imm = {{15{insn_q[16]}}, insn_q[16:0]};

  // Per-lane ALU result for the ALU-class execute step.
  function automatic logic [31:0] alu_result(
      input logic [5:0] o, input logic [31:0] a, input logic [31:0] b,
      input logic [31:0] im, input logic [15:0] tid);
    unique case (o)
      OP_TID:  alu_result = {16'b0, tid};
      OP_LI:   alu_result = im;
      OP_MOV:  alu_result = a;
      OP_ADD:  alu_result = a + b;
      OP_SUB:  alu_result = a - b;
      OP_MUL:  alu_result = a * b;
      OP_AND:  alu_result = a & b;
      OP_OR:   alu_result = a | b;
      OP_XOR:  alu_result = a ^ b;
      OP_SLL:  alu_result = a << b[4:0];
      OP_SRL:  alu_result = a >> b[4:0];
      OP_SRA:  alu_result = $signed(a) >>> b[4:0];
      OP_MIN:  alu_result = ($signed(a) < $signed(b)) ? a : b;
      OP_MAX:  alu_result = ($signed(a) > $signed(b)) ? a : b;
      OP_ADDI: alu_result = a + im;
      OP_MULI: alu_result = a * im;
      default: alu_result = 32'b0;
    endcase
  endfunction
  wire op_writes_rd = op != OP_HALT && op != OP_STX && op != OP_LDX;

  // Active mask for the current mem lane (predicated tail handling).
  wire [15:0] mem_tid    = tid_base_q + {13'b0, mem_lane_q};
  wire        mem_active = mem_tid < job_threads_q;
  wire [11:0] mem_addr   = regs[mem_lane_q][ra][11:0];

  // Program memory: port A data-port MMIO, port B engine fetch.
  always_ff @(posedge clk) begin
    if (d_prog_hit && d_wstrb == 4'hf && !busy_q) prog[d_prog_idx] <= d_wdata;
    else if (d_prog_hit && d_wstrb == 4'b0)       prog_mmio_rdata_q <= prog[d_prog_idx];
  end
  always_ff @(posedge clk) begin
    prog_eng_rdata_q <= prog[pc_q[5:0]];
  end

  // Data buffer: port A data-port MMIO, port B the engine load/store lane.
  wire        eng_data_we = busy_q && state_q == E_STX && mem_active;
  always_ff @(posedge clk) begin
    if (d_data_hit && d_wstrb == 4'hf && !busy_q) gmem[d_data_idx] <= d_wdata;
    else if (d_data_hit && d_wstrb == 4'b0)       data_mmio_rdata_q <= gmem[d_data_idx];
  end
  always_ff @(posedge clk) begin
    if (eng_data_we) gmem[mem_addr] <= regs[mem_lane_q][rb];
    else             data_eng_rdata_q <= gmem[mem_addr];
  end

  task automatic apply_reg_write(input logic [15:0] off,
                                 input logic [31:0] wdata,
                                 input logic [3:0] strb);
    unique case (off)
      OFF_DOORBELL: begin
        if (!busy_q) begin
          done_q <= 1'b0;
          if (nthreads_q == 32'b0) begin
            done_q  <= 1'b1;
            count_q <= count_q + 32'd1;
          end else begin
            busy_q        <= 1'b1;
            job_threads_q <= (nthreads_q > 32'(8 * DATA_WORDS)) ?
                             16'(8 * DATA_WORDS) : nthreads_q[15:0];
            job_ninsn_q   <= (ninsn_q > PROG_WORDS) ? 7'(PROG_WORDS) : ninsn_q[6:0];
          end
        end
      end
      OFF_STATUS: begin
        // DONE is write-1-to-clear; its bit lives in byte lane 0.
        if (strb[0] && wdata[1]) done_q <= 1'b0;
      end
      OFF_NTHREADS: if (!busy_q) nthreads_q <= merge_bytes(nthreads_q, wdata, strb);
      OFF_NINSN:    if (!busy_q) ninsn_q <= merge_bytes(ninsn_q, wdata, strb);
      default: ;
    endcase
  endtask

  always_ff @(posedge clk) begin
    if (rst) begin
      nthreads_q    <= 32'b0;
      ninsn_q       <= 32'b0;
      count_q       <= 32'b0;
      busy_q        <= 1'b0;
      done_q        <= 1'b0;
      state_q       <= E_IDLE;
      job_threads_q <= 16'b0;
      job_ninsn_q   <= 7'b0;
      tid_base_q    <= 16'b0;
      pc_q          <= 7'b0;
      mem_lane_q    <= 3'b0;
      buf_pending_q <= 1'b0;
      insn_q        <= 32'b0;
    end else begin
      buf_pending_q <= d_buf_read && !buf_pending_q;
      // Same conflict rule as the reference CLINT: if both ports write a
      // register in one cycle the D port wins.
      if (i_valid && !i_err && |i_wstrb) apply_reg_write(i_off, i_wdata, i_wstrb);
      if (d_valid && !d_err && |d_wstrb && !d_buf_hit)
        apply_reg_write(d_off, d_wdata, d_wstrb);

      if (busy_q) begin
        unique case (state_q)
          E_IDLE: begin  // entered on the doorbell: launch wave 0
            tid_base_q <= 16'b0;
            pc_q       <= 7'b0;
            state_q    <= E_FETCH;
          end
          E_FETCH: state_q <= E_DECODE;  // prog_eng_rdata_q settles this cycle
          E_DECODE: begin
            insn_q <= prog_eng_rdata_q;
            // op_is_* below still see the *previous* insn_q, so decode from the
            // freshly latched word: recompute the routing locally.
            if (prog_eng_rdata_q[31:26] == OP_HALT || pc_q >= job_ninsn_q)
              state_q <= E_WAVE;
            else if (prog_eng_rdata_q[31:26] == OP_LDX) begin
              mem_lane_q <= 3'd0;
              state_q    <= E_LDX_I;
            end else if (prog_eng_rdata_q[31:26] == OP_STX) begin
              mem_lane_q <= 3'd0;
              state_q    <= E_STX;
            end else
              state_q <= E_EXEC;
          end
          E_EXEC: begin
            for (int l = 0; l < 8; l++)
              if (op_writes_rd)
                regs[l][rd] <= alu_result(op, regs[l][ra], regs[l][rb], imm,
                                          tid_base_q + 16'(l));
            pc_q    <= pc_q + 7'd1;
            state_q <= E_FETCH;
          end
          E_LDX_I: state_q <= E_LDX_C;  // data_eng_rdata_q settles this cycle
          E_LDX_C: begin
            regs[mem_lane_q][rd] <= mem_active ? data_eng_rdata_q : 32'b0;
            if (mem_lane_q == 3'd7) begin
              pc_q    <= pc_q + 7'd1;
              state_q <= E_FETCH;
            end else begin
              mem_lane_q <= mem_lane_q + 3'd1;
              state_q    <= E_LDX_I;
            end
          end
          E_STX: begin
            // The store itself happens in the gmem port-B always_ff.
            if (mem_lane_q == 3'd7) begin
              pc_q    <= pc_q + 7'd1;
              state_q <= E_FETCH;
            end else
              mem_lane_q <= mem_lane_q + 3'd1;
          end
          E_WAVE: begin  // wave finished; launch the next or complete the job
            if (16'(tid_base_q) + 16'd8 >= job_threads_q) begin
              busy_q  <= 1'b0;
              done_q  <= 1'b1;
              count_q <= count_q + 32'd1;
              state_q <= E_IDLE;
            end else begin
              tid_base_q <= tid_base_q + 16'd8;
              pc_q       <= 7'b0;
              state_q    <= E_FETCH;
            end
          end
          default: state_q <= E_IDLE;
        endcase
      end
    end
  end
endmodule
