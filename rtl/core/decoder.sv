// aXcore main decoder — pure combinational: instruction word in, control
// signals out. Anything not a legal RV32I+Zicsr encoding raises `illegal`
// (which the pipeline turns into a precise trap at commit).
//
// Field extraction (rs1/rs2/rd indices, funct3 for branch kind and load/
// store size) is sliced from the instruction by the consuming stages; this
// module owns only *control*.
module decoder #(
  parameter bit ENABLE_M = 1'b1
) (
  input  logic [31:0]          insn,

  output logic                 illegal,
  output logic                 rd_we,
  output axcore_pkg::opa_sel_e opa_sel,
  output axcore_pkg::opb_sel_e opb_sel,
  output axcore_pkg::imm_sel_e imm_sel,
  output axcore_pkg::alu_op_t  alu_op,
  output axcore_pkg::wb_sel_e  wb_sel,
  output axcore_pkg::mem_op_e  mem_op,
  output axcore_pkg::br_sel_e  br_sel,
  output axcore_pkg::csr_op_e  csr_op,
  output logic                 csr_imm,   // uimm form (csrrwi/si/ci)
  output logic                 muldiv,    // RV32M operation; funct3 selects it
  output axcore_pkg::sys_e     sys,
  output logic                 uses_rs1,  // for the hazard/forwarding unit
  output logic                 uses_rs2
);

  import axcore_pkg::*;

  logic [2:0] f3;
  logic [6:0] f7;
  assign f3 = insn[14:12];
  assign f7 = insn[31:25];

  always_comb begin
    illegal  = 1'b0;
    rd_we    = 1'b0;
    opa_sel  = OPA_RS1;
    opb_sel  = OPB_RS2;
    imm_sel  = IMM_I;
    alu_op   = ALU_ADD;
    wb_sel   = WB_ALU;
    mem_op   = MEM_NONE;
    br_sel   = BR_NONE;
    csr_op   = CSR_NONE;
    csr_imm  = 1'b0;
    muldiv   = 1'b0;
    sys      = SYS_NONE;
    uses_rs1 = 1'b0;
    uses_rs2 = 1'b0;

    unique case (insn[6:0])
      7'b0110111: begin  // LUI: rd = 0 + immU
        rd_we   = 1'b1;
        opa_sel = OPA_ZERO;
        opb_sel = OPB_IMM;
        imm_sel = IMM_U;
      end

      7'b0010111: begin  // AUIPC: rd = pc + immU
        rd_we   = 1'b1;
        opa_sel = OPA_PC;
        opb_sel = OPB_IMM;
        imm_sel = IMM_U;
      end

      7'b1101111: begin  // JAL
        rd_we   = 1'b1;
        wb_sel  = WB_PC4;
        br_sel  = BR_JAL;
        imm_sel = IMM_J;
      end

      7'b1100111: begin  // JALR
        if (f3 != 3'b000) illegal = 1'b1;
        else begin
          rd_we    = 1'b1;
          wb_sel   = WB_PC4;
          br_sel   = BR_JALR;
          imm_sel  = IMM_I;
          uses_rs1 = 1'b1;
        end
      end

      7'b1100011: begin  // conditional branches (kind = funct3)
        if (f3 == 3'b010 || f3 == 3'b011) illegal = 1'b1;
        else begin
          br_sel   = BR_COND;
          imm_sel  = IMM_B;
          uses_rs1 = 1'b1;
          uses_rs2 = 1'b1;
        end
      end

      7'b0000011: begin  // loads (size/sign = funct3); address = rs1 + immI
        if (f3 == 3'b011 || f3 == 3'b110 || f3 == 3'b111) illegal = 1'b1;
        else begin
          rd_we    = 1'b1;
          wb_sel   = WB_MEM;
          mem_op   = MEM_LOAD;
          opb_sel  = OPB_IMM;
          uses_rs1 = 1'b1;
        end
      end

      7'b0100011: begin  // stores; address = rs1 + immS, data = rs2
        if (f3 > 3'b010) illegal = 1'b1;
        else begin
          mem_op   = MEM_STORE;
          opb_sel  = OPB_IMM;
          imm_sel  = IMM_S;
          uses_rs1 = 1'b1;
          uses_rs2 = 1'b1;
        end
      end

      7'b0010011: begin  // OP-IMM
        rd_we    = 1'b1;
        opb_sel  = OPB_IMM;
        uses_rs1 = 1'b1;
        alu_op   = {1'b0, f3};
        if (f3 == 3'b001 && f7 != 7'b0000000) illegal = 1'b1;  // SLLI
        if (f3 == 3'b101) begin                                // SRLI/SRAI
          if (f7 == 7'b0100000)      alu_op = {1'b1, f3};
          else if (f7 != 7'b0000000) illegal = 1'b1;
        end
      end

      7'b0110011: begin  // OP, including RV32M when funct7=0x01
        rd_we    = 1'b1;
        uses_rs1 = 1'b1;
        uses_rs2 = 1'b1;
        if (f7 == 7'b0000000)
          alu_op = {1'b0, f3};
        else if (f7 == 7'b0100000 && (f3 == 3'b000 || f3 == 3'b101))
          alu_op = {1'b1, f3};
        else if (f7 == 7'b0000001 && ENABLE_M)
          muldiv = 1'b1;
        else
          illegal = 1'b1;
      end

      7'b0001111: begin  // MISC-MEM
        unique case (f3)
          3'b000:  ;                   // FENCE: nop (single hart, uncached)
          3'b001:  sys = SYS_FENCE_I;  // FENCE.I: serializing flush
          default: illegal = 1'b1;     // reserved funct3
        endcase
      end

      7'b1110011: begin  // SYSTEM
        if (f3 == 3'b000 || f3 == 3'b100) begin
          // Privilege legality (mret/sret below their level, TW/TVM/TSR
          // traps) is dynamic state — resolved at the commit point, not here.
          if (insn == 32'h0000_0073)       sys = SYS_ECALL;
          else if (insn == 32'h0010_0073)  sys = SYS_EBREAK;
          else if (insn == 32'h3020_0073)  sys = SYS_MRET;
          else if (insn == 32'h1020_0073)  sys = SYS_SRET;
          else if (insn == 32'h1050_0073)  sys = SYS_WFI;
          else if (f7 == 7'b0001001 && f3 == 3'b000 && insn[11:7] == 5'b0)
            sys = SYS_SFENCE;              // sfence.vma rs1, rs2
          else illegal = 1'b1;
        end else begin  // Zicsr
          rd_we    = 1'b1;
          wb_sel   = WB_CSR;
          csr_imm  = f3[2];
          uses_rs1 = ~f3[2];
          unique case (f3[1:0])
            2'b01:   csr_op = CSR_RW;
            2'b10:   csr_op = CSR_RS;
            2'b11:   csr_op = CSR_RC;
            default: illegal = 1'b1;  // f3[1:0]=00 handled above
          endcase
        end
      end

      default: illegal = 1'b1;
    endcase
  end

endmodule
