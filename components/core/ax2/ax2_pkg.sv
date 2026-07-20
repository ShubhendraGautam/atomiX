// Types shared inside the ax2 core.  Only the decoded-instruction bundle
// crosses a module boundary here; everything architectural comes from
// axcore_pkg, which ax2 shares with the reference core so both cores decode
// RV32IM through the identical verified decoder.
package ax2_pkg;
  import axcore_pkg::*;

  // One decoded instruction, as carried down the pipeline.  Packed so a whole
  // issue slot is a single pipeline register.
  typedef struct packed {
    logic                  illegal;
    logic                  rd_we;
    axcore_pkg::opa_sel_e  opa_sel;
    axcore_pkg::opb_sel_e  opb_sel;
    axcore_pkg::imm_sel_e  imm_sel;
    axcore_pkg::alu_op_t   alu_op;
    axcore_pkg::wb_sel_e   wb_sel;
    axcore_pkg::mem_op_e   mem_op;
    axcore_pkg::br_sel_e   br_sel;
    axcore_pkg::csr_op_e   csr_op;
    logic                  csr_imm;
    logic                  muldiv;
    axcore_pkg::sys_e      sys;
  } dec_t;

endpackage
