// aXcore branch comparator — decides conditional branches in EX.
// Kind comes straight from funct3 (gaps 010/011 are rejected in decode).
module branch_cmp (
  input  logic [31:0] a,
  input  logic [31:0] b,
  input  logic [2:0]  f3,
  output logic        taken
);

  always_comb begin
    unique case (f3)
      3'b000:  taken = a == b;                    // BEQ
      3'b001:  taken = a != b;                    // BNE
      3'b100:  taken = $signed(a) < $signed(b);   // BLT
      3'b101:  taken = $signed(a) >= $signed(b);  // BGE
      3'b110:  taken = a < b;                     // BLTU
      3'b111:  taken = a >= b;                    // BGEU
      default: taken = 1'b0;
    endcase
  end

endmodule
