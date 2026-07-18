// aXcore shared types. Kept minimal: only what crosses module boundaries.
package axcore_pkg;

  // Immediate formats, RISC-V unprivileged spec §2.3.
  typedef enum logic [2:0] {
    IMM_I,
    IMM_S,
    IMM_B,
    IMM_U,
    IMM_J
  } imm_sel_e;

  // Decoder control selects.
  typedef enum logic [1:0] {
    OPA_RS1,
    OPA_PC,
    OPA_ZERO
  } opa_sel_e;

  typedef enum logic [0:0] {
    OPB_RS2,
    OPB_IMM
  } opb_sel_e;

  typedef enum logic [1:0] {
    WB_ALU,   // ALU result
    WB_MEM,   // load data
    WB_PC4,   // link address (JAL/JALR)
    WB_CSR    // CSR old value
  } wb_sel_e;

  typedef enum logic [1:0] {
    MEM_NONE,
    MEM_LOAD,
    MEM_STORE
  } mem_op_e;

  typedef enum logic [1:0] {
    BR_NONE,
    BR_COND,  // conditional branch, kind in funct3
    BR_JAL,
    BR_JALR
  } br_sel_e;

  typedef enum logic [1:0] {
    CSR_NONE,
    CSR_RW,
    CSR_RS,
    CSR_RC
  } csr_op_e;

  // Serialized instruction classes (DESIGN.md §4.2) + traps raised in decode.
  typedef enum logic [2:0] {
    SYS_NONE,
    SYS_ECALL,
    SYS_EBREAK,
    SYS_MRET,
    SYS_WFI,     // executes as nop in v1
    SYS_FENCE_I  // serializing flush; memory effect is a nop until caches
  } sys_e;

  // ALU operation: {modifier, funct3}. The modifier bit is funct7[5]
  // (SUB/SRA) for OP/OP-IMM instructions, 0 otherwise.
  typedef logic [3:0] alu_op_t;
  localparam alu_op_t ALU_ADD  = 4'b0000;
  localparam alu_op_t ALU_SUB  = 4'b1000;
  localparam alu_op_t ALU_SLL  = 4'b0001;
  localparam alu_op_t ALU_SLT  = 4'b0010;
  localparam alu_op_t ALU_SLTU = 4'b0011;
  localparam alu_op_t ALU_XOR  = 4'b0100;
  localparam alu_op_t ALU_SRL  = 4'b0101;
  localparam alu_op_t ALU_SRA  = 4'b1101;
  localparam alu_op_t ALU_OR   = 4'b0110;
  localparam alu_op_t ALU_AND  = 4'b0111;

endpackage
