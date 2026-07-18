// QEMU-virt-compatible CLINT subset: hart 0 MSIP, MTIMECMP, and MTIME.
// Two aXbus ports permit an instruction fetch from ROM/RAM while the data
// port services the normal CLINT programming model.
module clint #(
  parameter logic [31:0] BASE = 32'h0200_0000
) (
  input  logic        clk,
  input  logic        rst,
  input  logic        i_valid,
  input  logic [31:0] i_addr,
  input  logic [31:0] i_wdata,
  input  logic [3:0]  i_wstrb,
  output logic        i_ready,
  output logic [31:0] i_rdata,
  output logic        i_err,
  input  logic        d_valid,
  input  logic [31:0] d_addr,
  input  logic [31:0] d_wdata,
  input  logic [3:0]  d_wstrb,
  output logic        d_ready,
  output logic [31:0] d_rdata,
  output logic        d_err,
  output logic        irq_software,
  output logic        irq_timer
);
  localparam logic [15:0] MSIP      = 16'h0000;
  localparam logic [15:0] MTIMECMP  = 16'h4000;
  localparam logic [15:0] MTIMECMPH = 16'h4004;
  localparam logic [15:0] MTIME     = 16'hbff8;
  localparam logic [15:0] MTIMEH    = 16'hbffc;
  logic msip_q;
  logic [63:0] mtime_q, mtimecmp_q;
  wire i_in_range = i_addr >= BASE && i_addr - BASE < 32'h0001_0000;
  wire d_in_range = d_addr >= BASE && d_addr - BASE < 32'h0001_0000;
  wire [15:0] i_off = i_addr[15:0];
  wire [15:0] d_off = d_addr[15:0];

  function automatic logic known_offset(input logic [15:0] off);
    known_offset = off == MSIP || off == MTIMECMP || off == MTIMECMPH ||
                   off == MTIME || off == MTIMEH;
  endfunction
  function automatic logic [31:0] read_offset(input logic [15:0] off);
    unique case (off)
      MSIP:      read_offset = {31'b0, msip_q};
      MTIMECMP:  read_offset = mtimecmp_q[31:0];
      MTIMECMPH: read_offset = mtimecmp_q[63:32];
      MTIME:     read_offset = mtime_q[31:0];
      MTIMEH:    read_offset = mtime_q[63:32];
      default:   read_offset = 32'b0;
    endcase
  endfunction
  function automatic logic [31:0] merge_bytes(
      input logic [31:0] old_value, input logic [31:0] new_value,
      input logic [3:0] strb);
    merge_bytes = old_value;
    if (strb[0]) merge_bytes[7:0]   = new_value[7:0];
    if (strb[1]) merge_bytes[15:8]  = new_value[15:8];
    if (strb[2]) merge_bytes[23:16] = new_value[23:16];
    if (strb[3]) merge_bytes[31:24] = new_value[31:24];
  endfunction

  always_comb begin
    i_ready = i_valid;
    i_err   = i_valid && (!i_in_range || !known_offset(i_off) || i_addr[1:0] != 2'b00);
    i_rdata = read_offset(i_off);
    d_ready = d_valid;
    d_err   = d_valid && (!d_in_range || !known_offset(d_off) || d_addr[1:0] != 2'b00);
    d_rdata = read_offset(d_off);
  end

  always_ff @(posedge clk) begin
    if (rst) begin
      msip_q     <= 1'b0;
      mtime_q    <= 64'b0;
      mtimecmp_q <= 64'hffff_ffff_ffff_ffff;
    end else begin
      mtime_q <= mtime_q + 64'd1;
      if (i_valid && !i_err && |i_wstrb) begin
        unique case (i_off)
          MSIP:      msip_q <= |(merge_bytes({31'b0, msip_q}, i_wdata, i_wstrb) & 32'h1);
          MTIMECMP:  mtimecmp_q[31:0]  <= merge_bytes(mtimecmp_q[31:0], i_wdata, i_wstrb);
          MTIMECMPH: mtimecmp_q[63:32] <= merge_bytes(mtimecmp_q[63:32], i_wdata, i_wstrb);
          MTIME:     mtime_q[31:0]     <= merge_bytes(mtime_q[31:0], i_wdata, i_wstrb);
          MTIMEH:    mtime_q[63:32]    <= merge_bytes(mtime_q[63:32], i_wdata, i_wstrb);
          default: ;
        endcase
      end
      // If both ports write a register in the same cycle, D port wins. The
      // CPU normally uses it for device access; I-port writes are unusual.
      if (d_valid && !d_err && |d_wstrb) begin
        unique case (d_off)
          MSIP:      msip_q <= |(merge_bytes({31'b0, msip_q}, d_wdata, d_wstrb) & 32'h1);
          MTIMECMP:  mtimecmp_q[31:0]  <= merge_bytes(mtimecmp_q[31:0], d_wdata, d_wstrb);
          MTIMECMPH: mtimecmp_q[63:32] <= merge_bytes(mtimecmp_q[63:32], d_wdata, d_wstrb);
          MTIME:     mtime_q[31:0]     <= merge_bytes(mtime_q[31:0], d_wdata, d_wstrb);
          MTIMEH:    mtime_q[63:32]    <= merge_bytes(mtime_q[63:32], d_wdata, d_wstrb);
          default: ;
        endcase
      end
    end
  end

  assign irq_software = msip_q;
  assign irq_timer = mtime_q >= mtimecmp_q;
endmodule
