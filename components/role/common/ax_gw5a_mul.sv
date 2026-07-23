// Arithmetic wrappers with an explicit GW5A implementation.
//
// Upstream synth_gowin does not currently lower generic multiplies into the
// Arora-V DSP cells. The board manifest defines AX_GW5A_DSP for GW5A builds;
// every other FPGA and all simulations retain ordinary portable RTL.

/* verilator lint_off DECLFILENAME */
module ax_mul32_low (
  input  logic [31:0] a,
  input  logic [31:0] b,
  output logic [31:0] y
);
`ifdef AX_GW5A_DSP
  // Only the low 32 bits are architecturally visible. Split each operand into
  // 16-bit limbs. The high-high term starts at bit 32 and is therefore not
  // needed. Each lane uses three of the GW5A's 27x18 DSP multipliers.
  wire [15:0] a0 = a[15:0];
  wire [15:0] a1 = a[31:16];
  wire [15:0] b0 = b[15:0];
  wire [15:0] b1 = b[31:16];
  wire [47:0] p00, p01, p10;
  wire [47:0] unused_caso00, unused_caso01, unused_caso10;
  wire [26:0] unused_soa00, unused_soa01, unused_soa10;

  MULTALU27X18 #(
    .C_SEL(1'b0), .CASI_SEL(1'b0), .ACC_SEL(1'b0)
  ) u_p00 (
    .DOUT(p00), .CASO(unused_caso00), .SOA(unused_soa00),
    .A({11'b0, a0}), .SIA(27'b0), .B({2'b0, b0}),
    .C(48'b0), .D(26'b0), .CASI(48'b0),
    .ACCSEL(1'b0), .PSEL(1'b0), .ASEL(1'b0), .PADDSUB(1'b0),
    .CSEL(1'b0), .CASISEL(1'b0), .ADDSUB(2'b0),
    .CLK(2'b0), .CE(2'b0), .RESET(2'b0)
  );
  MULTALU27X18 #(
    .C_SEL(1'b0), .CASI_SEL(1'b0), .ACC_SEL(1'b0)
  ) u_p01 (
    .DOUT(p01), .CASO(unused_caso01), .SOA(unused_soa01),
    .A({11'b0, a0}), .SIA(27'b0), .B({2'b0, b1}),
    .C(48'b0), .D(26'b0), .CASI(48'b0),
    .ACCSEL(1'b0), .PSEL(1'b0), .ASEL(1'b0), .PADDSUB(1'b0),
    .CSEL(1'b0), .CASISEL(1'b0), .ADDSUB(2'b0),
    .CLK(2'b0), .CE(2'b0), .RESET(2'b0)
  );
  MULTALU27X18 #(
    .C_SEL(1'b0), .CASI_SEL(1'b0), .ACC_SEL(1'b0)
  ) u_p10 (
    .DOUT(p10), .CASO(unused_caso10), .SOA(unused_soa10),
    .A({11'b0, a1}), .SIA(27'b0), .B({2'b0, b0}),
    .C(48'b0), .D(26'b0), .CASI(48'b0),
    .ACCSEL(1'b0), .PSEL(1'b0), .ASEL(1'b0), .PADDSUB(1'b0),
    .CSEL(1'b0), .CASISEL(1'b0), .ADDSUB(2'b0),
    .CLK(2'b0), .CE(2'b0), .RESET(2'b0)
  );

  wire [47:0] product = p00 + (p01 << 16) + (p10 << 16);
  assign y = product[31:0];
`else
  assign y = a * b;
`endif
endmodule


module ax_mul_s8 (
  input  logic signed [7:0] a,
  input  logic signed [7:0] b,
  output logic signed [15:0] y
);
`ifdef AX_GW5A_DSP
  // Magnitude conversion makes signed behavior independent of primitive mode.
  wire [7:0] a_mag = a[7] ? (~a + 8'd1) : a;
  wire [7:0] b_mag = b[7] ? (~b + 8'd1) : b;
  wire [23:0] magnitude;
  MULT12X12 u_mul (.DOUT(magnitude), .A({4'b0, a_mag}),
                   .B({4'b0, b_mag}), .CLK(2'b0), .CE(2'b0),
                   .RESET(2'b0));
  wire [15:0] mag16 = magnitude[15:0];
  assign y = (a[7] ^ b[7]) ? -$signed(mag16) : $signed(mag16);
`else
  assign y = a * b;
`endif
endmodule
/* verilator lint_on DECLFILENAME */
