// aXcore ALU — pure combinational, all ten RV32I register-register ops.
// op encoding is {funct7[5], funct3}, so decode is wiring, not translation.
module alu (
  input  logic [31:0]        a,
  input  logic [31:0]        b,
  input  axcore_pkg::alu_op_t op,
  output logic [31:0]        y
);

  always_comb begin
    unique case (op[2:0])
      3'b000:  y = op[3] ? a - b : a + b;                      // ADD/SUB
      3'b001:  y = a << b[4:0];                                // SLL
      3'b010:  y = {31'b0, $signed(a) < $signed(b)};           // SLT
      3'b011:  y = {31'b0, a < b};                             // SLTU
      3'b100:  y = a ^ b;                                      // XOR
      3'b101:  y = op[3] ? $unsigned($signed(a) >>> b[4:0])    // SRA
                         : a >> b[4:0];                        // SRL
      3'b110:  y = a | b;                                      // OR
      3'b111:  y = a & b;                                      // AND
      default: y = 32'b0;
    endcase
  end

endmodule
