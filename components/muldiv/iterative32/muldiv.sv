// aXcore RV32M execution unit.  All operations take 32 cycles so EX has one
// simple, deterministic stall protocol.  Multiplication is shift/add;
// division uses restoring unsigned division over magnitudes, with the RV32M
// signed and divide-by-zero corner rules applied at the result boundary.
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

  logic        busy_q, is_mul_q, negate_mul_q, negate_quot_q, negate_rem_q;
  logic [4:0]  count_q;
  logic [2:0]  op_q;
  logic [63:0] product_q, mcand_q;
  logic [31:0] mplier_q, quotient_q, special_result_q;
  logic [31:0] rem_q, divisor_q;
  logic        special_q;

  wire is_mul_in = op < 3'd4;
  wire signed_a_in = is_mul_in ? (op == 3'd1 || op == 3'd2)
                                : (op == 3'd4 || op == 3'd6);
  wire signed_b_in = is_mul_in ? (op == 3'd1)
                                : (op == 3'd4 || op == 3'd6);
  wire [31:0] a_mag_in = signed_a_in && a[31] ? (~a + 32'd1) : a;
  wire [31:0] b_mag_in = signed_b_in && b[31] ? (~b + 32'd1) : b;
  wire div_zero_in = !is_mul_in && b == 32'b0;
  wire div_overflow_in = !is_mul_in && signed_a_in &&
                         a == 32'h8000_0000 && b == 32'hffff_ffff;

  wire [63:0] product_step = product_q + (mplier_q[0] ? mcand_q : 64'b0);
  wire [32:0] rem_shift = {rem_q[31:0], quotient_q[31]};
  wire [31:0] quot_shift = {quotient_q[30:0], 1'b0};
  wire div_sub = rem_shift >= {1'b0, divisor_q};
  logic [31:0] rem_step;
  always_comb begin
    // The restoring invariant guarantees bit 32 is zero after either branch:
    // no-subtract means rem_shift < divisor, subtract means the result is.
    if (div_sub) rem_step = rem_shift[31:0] - divisor_q;
    else         rem_step = rem_shift[31:0];
  end
  wire [31:0] quot_step = div_sub ? (quot_shift | 32'd1) : quot_shift;

  wire [63:0] product_final = negate_mul_q ? (~product_step + 64'd1)
                                             : product_step;
  wire [31:0] quot_final = negate_quot_q ? (~quot_step + 32'd1) : quot_step;
  wire [31:0] rem_final = negate_rem_q ? (~rem_step + 32'd1) : rem_step;

  assign busy = busy_q;
  assign done = busy_q && count_q == 5'd31;
  always_comb begin
    if (is_mul_q) begin
      result = op_q == 3'd0 ? product_final[31:0] : product_final[63:32];
    end else if (special_q) begin
      result = special_result_q;
    end else begin
      result = op_q == 3'd4 || op_q == 3'd5 ? quot_final : rem_final;
    end
  end

  always_ff @(posedge clk) begin
    if (rst) begin
      busy_q <= 1'b0;
      count_q <= 5'b0;
    end else if (start && !busy_q) begin
      busy_q <= 1'b1;
      count_q <= 5'b0;
      op_q <= op;
      is_mul_q <= is_mul_in;
      negate_mul_q <= (signed_a_in && a[31]) ^ (signed_b_in && b[31]);
      negate_quot_q <= (signed_a_in && a[31]) ^ (signed_b_in && b[31]);
      negate_rem_q <= signed_a_in && a[31];
      product_q <= 64'b0;
      mcand_q <= {32'b0, a_mag_in};
      mplier_q <= b_mag_in;
      rem_q <= 32'b0;
      divisor_q <= b_mag_in;
      quotient_q <= a_mag_in;
      special_q <= div_zero_in || div_overflow_in;
      if (div_zero_in)
        special_result_q <= op == 3'd4 || op == 3'd5 ? 32'hffff_ffff : a;
      else if (div_overflow_in)
        special_result_q <= op == 3'd4 ? 32'h8000_0000 : 32'b0;
      else
        special_result_q <= 32'b0;
    end else if (busy_q) begin
      count_q <= count_q + 5'd1;
      if (is_mul_q) begin
        product_q <= product_step;
        mcand_q <= mcand_q << 1;
        mplier_q <= mplier_q >> 1;
      end else begin
        rem_q <= rem_step;
        quotient_q <= quot_step;
      end
      if (count_q == 5'd31) busy_q <= 1'b0;
    end
  end

endmodule
