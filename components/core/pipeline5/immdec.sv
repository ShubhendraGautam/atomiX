// aXcore immediate decoder — extracts and sign-extends all five immediate
// formats. Pure wiring; the format select comes from the main decoder.
module immdec (
  input  logic [31:0]          insn,
  input  axcore_pkg::imm_sel_e sel,
  output logic [31:0]          imm
);

  import axcore_pkg::*;

  // insn[6:0] is the opcode field; no immediate format draws from it.
  // verilator lint_off UNUSED
  logic [6:0] unused_opcode;
  assign unused_opcode = insn[6:0];
  // verilator lint_on UNUSED

  always_comb begin
    unique case (sel)
      IMM_I: imm = {{20{insn[31]}}, insn[31:20]};
      IMM_S: imm = {{20{insn[31]}}, insn[31:25], insn[11:7]};
      IMM_B: imm = {{19{insn[31]}}, insn[31], insn[7], insn[30:25],
                    insn[11:8], 1'b0};
      IMM_U: imm = {insn[31:12], 12'b0};
      IMM_J: imm = {{11{insn[31]}}, insn[31], insn[19:12], insn[20],
                    insn[30:21], 1'b0};
      default: imm = 32'b0;
    endcase
  end

endmodule
