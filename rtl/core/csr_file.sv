// aXcore M-mode CSR file — DESIGN.md §4.2: its own module, accessed at the
// commit point under the serialization rule, so no CSR forwarding exists.
// Semantics mirror sim/axsim/src/cpu.cpp csr_read/csr_write exactly; any
// intentional divergence must change both and be called out.
module csr_file (
  input  logic        clk,
  input  logic        rst,

  // Access port: driven at commit for a serialized Zicsr instruction.
  input  logic                 acc_en,     // check + read
  input  logic [11:0]          acc_addr,
  input  axcore_pkg::csr_op_e  acc_op,
  input  logic [31:0]          acc_wdata,  // rs1 value or zero-extended uimm
  input  logic                 acc_rs1x0,  // rs1/uimm is zero: RS/RC skip write
  input  logic                 acc_commit, // perform the side effect
  output logic [31:0]          acc_rdata,
  output logic                 acc_illegal,

  // Trap entry (mutually exclusive with acc/mret by construction: one
  // instruction commits per cycle).
  input  logic        trap_en,
  input  logic [31:0] trap_pc,
  input  logic [3:0]  trap_cause,
  input  logic [31:0] trap_tval,
  output logic [31:0] trap_vector,

  input  logic        mret_en,
  output logic [31:0] mepc_out,

  input  logic        retire      // minstret increment
);

  import axcore_pkg::*;

  logic        mie_q, mpie_q;     // the two writable mstatus bits
  logic [31:0] mtvec_q, mepc_q, mcause_q, mtval_q, mscratch_q, mie_reg_q;
  logic [63:0] cycle_q, instret_q;

  wire [31:0] mstatus = {19'b0, 2'b11, 3'b0, mpie_q, 3'b0, mie_q, 3'b0};

  assign trap_vector = {mtvec_q[31:2], 2'b00};
  assign mepc_out    = mepc_q;

  // Read + legality (combinational).
  logic known;
  always_comb begin
    known     = 1'b1;
    acc_rdata = 32'b0;
    unique case (acc_addr)
      12'h300: acc_rdata = mstatus;
      12'h301: acc_rdata = 32'h4000_0100;  // misa: RV32I
      12'h304: acc_rdata = mie_reg_q;
      12'h305: acc_rdata = mtvec_q;
      12'h340: acc_rdata = mscratch_q;
      12'h341: acc_rdata = mepc_q;
      12'h342: acc_rdata = mcause_q;
      12'h343: acc_rdata = mtval_q;
      12'h344: acc_rdata = 32'b0;          // mip: no sources until CLINT
      12'hF11, 12'hF12, 12'hF13, 12'hF14: acc_rdata = 32'b0;
      12'hB00, 12'hC00: acc_rdata = cycle_q[31:0];
      12'hB80, 12'hC80: acc_rdata = cycle_q[63:32];
      12'hB02, 12'hC02: acc_rdata = instret_q[31:0];
      12'hB82, 12'hC82: acc_rdata = instret_q[63:32];
      default: known = 1'b0;
    endcase
  end

  wire wen = acc_en && (acc_op == CSR_RW || !acc_rs1x0);
  assign acc_illegal =
      acc_en && (!known || (wen && acc_addr[11:10] == 2'b11));

  wire [31:0] wval = (acc_op == CSR_RW) ? acc_wdata
                   : (acc_op == CSR_RS) ? (acc_rdata | acc_wdata)
                                        : (acc_rdata & ~acc_wdata);

  always_ff @(posedge clk) begin
    if (rst) begin
      mie_q      <= 1'b0;
      mpie_q     <= 1'b0;
      mtvec_q    <= 32'b0;
      mepc_q     <= 32'b0;
      mcause_q   <= 32'b0;
      mtval_q    <= 32'b0;
      mscratch_q <= 32'b0;
      mie_reg_q  <= 32'b0;
      cycle_q    <= 64'b0;
      instret_q  <= 64'b0;
    end else begin
      cycle_q <= cycle_q + 64'd1;
      if (retire) instret_q <= instret_q + 64'd1;

      if (trap_en) begin
        mepc_q   <= trap_pc;
        mcause_q <= {28'b0, trap_cause};
        mtval_q  <= trap_tval;
        mpie_q   <= mie_q;
        mie_q    <= 1'b0;
      end else if (mret_en) begin
        mie_q  <= mpie_q;
        mpie_q <= 1'b1;
      end else if (wen && acc_commit && !acc_illegal) begin
        unique case (acc_addr)
          12'h300: begin mie_q <= wval[3]; mpie_q <= wval[7]; end
          12'h301: ;                             // misa: writes ignored
          12'h304: mie_reg_q <= wval;
          12'h305: mtvec_q <= wval & ~32'h2;     // direct/vectored only
          12'h340: mscratch_q <= wval;
          12'h341: mepc_q <= wval & ~32'h3;      // IALIGN=32
          12'h342: mcause_q <= wval;
          12'h343: mtval_q <= wval;
          12'h344: ;                             // mip: hardware-set
          12'hB00, 12'hB02, 12'hB80, 12'hB82: ;  // counters: ignored
          default: ;
        endcase
      end
    end
  end

endmodule
