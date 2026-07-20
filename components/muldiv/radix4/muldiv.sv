// muldiv.radix4: RV32M execution unit with a single-cycle multiplier and a
// radix-4 divider -- 16 divide cycles instead of 32.
//
// The multiplier is the one from muldiv.fast-mul: a single signed 33x33 product
// covers MUL/MULH/MULHSU/MULHU, mapping onto FPGA DSP blocks.
//
// The divider is where this component earns its place.  The reference divider
// is restoring radix-2: one quotient bit per cycle, so every division costs 32
// cycles.  That is fine for code that divides occasionally and punishing for
// fixed-point code -- renderers, DSP, anything doing perspective or scaling
// arithmetic -- where divides sit in the inner loop.  Radix-4 resolves two
// quotient bits per cycle by comparing the shifted remainder against 1x, 2x and
// 3x the divisor and selecting the largest that fits, which halves the latency
// for one extra comparator and a 3x adder.
//
// The start/busy/done contract is identical to muldiv.iterative32 and
// muldiv.fast-mul -- operands sampled at the start edge, done for one cycle,
// result held during done -- so this passes the same latency-agnostic unit
// testbench and the same cosim/ISA suites without any consumer change.
module muldiv (
  input  logic        clk,
  input  logic        rst,
  input  logic        start,
  input  logic [2:0]  op,       // funct3: MUL..REMU
  input  logic [31:0] a,
  input  logic [31:0] b,
  output logic        busy,
  output logic        done,
  output logic [31:0] result
);

  logic        busy_q, is_mul_q, negate_quot_q, negate_rem_q;
  logic [3:0]  count_q;                 // 16 radix-4 steps
  logic [2:0]  op_q;
  logic [63:0] product_q;
  logic [31:0] quotient_q, special_result_q;
  logic [31:0] rem_q, divisor_q;
  logic        special_q;

  wire is_mul_in = op < 3'd4;
  wire signed_a_in = is_mul_in ? (op == 3'd1 || op == 3'd2)
                               : (op == 3'd4 || op == 3'd6);
  wire signed_b_in = is_mul_in ? (op == 3'd1)
                               : (op == 3'd4 || op == 3'd6);

  wire [32:0] a_ext = {signed_a_in & a[31], a};
  wire [32:0] b_ext = {signed_b_in & b[31], b};
  wire signed [65:0] mul_full = $signed(a_ext) * $signed(b_ext);

  wire [31:0] a_mag_in = signed_a_in && a[31] ? (~a + 32'd1) : a;
  wire [31:0] b_mag_in = signed_b_in && b[31] ? (~b + 32'd1) : b;
  wire div_zero_in = !is_mul_in && b == 32'b0;
  wire div_overflow_in = !is_mul_in && signed_a_in &&
                         a == 32'h8000_0000 && b == 32'hffff_ffff;

  // ---- Radix-4 restoring step -------------------------------------------------
  // The invariant is rem < divisor, so rem always fits in 32 bits.  Shifting it
  // left by two and pulling in the next two dividend bits needs 34, and 3x the
  // divisor needs 34, so the comparison happens at 34 bits.
  wire [33:0] rem_shift = {rem_q, quotient_q[31:30]};
  wire [33:0] div_1x = {2'b0, divisor_q};
  wire [33:0] div_2x = {1'b0, divisor_q, 1'b0};
  wire [33:0] div_3x = div_1x + div_2x;

  logic [1:0]  quot_digit;
  logic [33:0] rem_next;
  always_comb begin
    // Largest multiple that fits; the restoring invariant guarantees the
    // selected difference is again below the divisor, hence below 2^32.
    if (rem_shift >= div_3x)      begin quot_digit = 2'd3; rem_next = rem_shift - div_3x; end
    else if (rem_shift >= div_2x) begin quot_digit = 2'd2; rem_next = rem_shift - div_2x; end
    else if (rem_shift >= div_1x) begin quot_digit = 2'd1; rem_next = rem_shift - div_1x; end
    else                          begin quot_digit = 2'd0; rem_next = rem_shift; end
  end
  wire [31:0] rem_step  = rem_next[31:0];
  wire [31:0] quot_step = {quotient_q[29:0], quot_digit};

  wire [31:0] quot_final = negate_quot_q ? (~quot_step + 32'd1) : quot_step;
  wire [31:0] rem_final  = negate_rem_q  ? (~rem_step + 32'd1)  : rem_step;

  assign busy = busy_q;
  assign done = busy_q && (is_mul_q || count_q == 4'd15);
  always_comb begin
    if (is_mul_q) begin
      result = op_q == 3'd0 ? product_q[31:0] : product_q[63:32];
    end else if (special_q) begin
      result = special_result_q;
    end else begin
      result = op_q == 3'd4 || op_q == 3'd5 ? quot_final : rem_final;
    end
  end

  always_ff @(posedge clk) begin
    if (rst) begin
      busy_q  <= 1'b0;
      count_q <= 4'b0;
    end else if (start && !busy_q) begin
      busy_q  <= 1'b1;
      count_q <= 4'b0;
      op_q    <= op;
      is_mul_q <= is_mul_in;
      product_q <= mul_full[63:0];
      negate_quot_q <= (signed_a_in && a[31]) ^ (signed_b_in && b[31]);
      negate_rem_q  <= signed_a_in && a[31];
      rem_q      <= 32'b0;
      divisor_q  <= b_mag_in;
      quotient_q <= a_mag_in;
      special_q  <= div_zero_in || div_overflow_in;
      if (div_zero_in)
        special_result_q <= op == 3'd4 || op == 3'd5 ? 32'hffff_ffff : a;
      else if (div_overflow_in)
        special_result_q <= op == 3'd4 ? 32'h8000_0000 : 32'b0;
      else
        special_result_q <= 32'b0;
    end else if (busy_q) begin
      count_q <= count_q + 4'd1;
      if (!is_mul_q) begin
        rem_q      <= rem_step;
        quotient_q <= quot_step;
      end
      if (is_mul_q || count_q == 4'd15) busy_q <= 1'b0;
    end
  end

  // verilator lint_off UNUSED
  wire unused = ^{mul_full[65:64], rem_next[33:32]};
  // verilator lint_on UNUSED

endmodule
