// Sv32 MMU: a small fully-associative TLB plus a hardware page-table walker,
// one instance per aXbus master port (fetch and data). Bare mode (effective
// privilege M, or satp.MODE = 0) is a pure combinational pass-through, so the
// M-only pipeline pays nothing.
//
// Protocol toward the core: aXbus with one extra completion flavor —
// c_ready && c_pgfault reports a translation page fault (no bus transaction
// occurred; the cause is Sv32-specific, unlike the physical c_err). The core
// holds c_valid/c_addr stable until completion (aXbus rule), which lets the
// walker take as many bus cycles as it needs and then satisfy the original
// request from the freshly filled TLB.
//
// A/D bits are hardware-managed (matching the ISS and QEMU): the walker
// writes the PTE back with A (and D on stores) set. The TLB only caches
// entries whose A/D state already covers the access; a store hitting a
// clean entry re-walks so D is set in memory.
module sv32_mmu #(
  parameter bit IS_FETCH = 1'b0
) (
  input  logic        clk,
  input  logic        rst,

  // Translation context (from the CSR file; effective privilege for this
  // port — the data port applies MPRV substitution in the core).
  input  logic [31:0] satp,
  input  logic [1:0]  eprv,
  input  logic        sum,
  input  logic        mxr,
  input  logic        flush,     // invalidate the TLB (serialized commits)

  // Core side.
  input  logic        c_valid,
  input  logic [31:0] c_addr,
  input  logic [31:0] c_wdata,
  input  logic [3:0]  c_wstrb,
  output logic        c_ready,
  output logic [31:0] c_rdata,
  output logic        c_err,
  output logic        c_pgfault,

  // Bus master side.
  output logic        m_valid,
  output logic [31:0] m_addr,
  output logic [31:0] m_wdata,
  output logic [3:0]  m_wstrb,
  input  logic        m_ready,
  input  logic [31:0] m_rdata,
  input  logic        m_err
);

  wire bare     = eprv == 2'b11 || !satp[31];
  wire is_store = !IS_FETCH && |c_wstrb;

  // ------------------------------------------------------------------ TLB
  localparam int N = 8;
  logic          tlb_v[N];
  logic          tlb_super[N];
  logic [19:0]   tlb_vpn[N];
  logic [31:0]   tlb_pte[N];
  logic [$clog2(N)-1:0] fill_ptr;

  wire [19:0] vpn = c_addr[31:12];

  // Leaf-PTE permission check (RWXU bits) for the current access/context.
  function automatic logic perm_ok(input logic [4:1] p);
    logic r, w, xb, u;
    r = p[1]; w = p[2]; xb = p[3]; u = p[4];
    perm_ok = IS_FETCH ? xb : is_store ? w : (r || (mxr && xb));
    if (eprv == 2'b00 && !u) perm_ok = 1'b0;
    if (eprv == 2'b01 && u && (IS_FETCH || !sum)) perm_ok = 1'b0;
  endfunction

  logic        hit;
  logic [31:0] hit_pte;
  logic        hit_super;
  always_comb begin
    hit = 1'b0;
    hit_pte = 32'b0;
    hit_super = 1'b0;
    for (int i = 0; i < N; i++) begin
      if (tlb_v[i] &&
          (tlb_super[i] ? tlb_vpn[i][19:10] == vpn[19:10]
                        : tlb_vpn[i] == vpn)) begin
        hit = 1'b1;
        hit_pte = tlb_pte[i];
        hit_super = tlb_super[i];
      end
    end
  end

  // A cached entry only serves accesses its A/D state already covers.
  wire hit_usable = hit && hit_pte[6] && (!is_store || hit_pte[7]);
  wire hit_perm   = perm_ok(hit_pte[4:1]);
  // 32-bit physical space: ppn[19:0] = pte[29:10]; a superpage contributes
  // ppn1[9:0] = pte[29:20] (the ISS truncates identically).
  wire [31:0] hit_pa = hit_super ? {hit_pte[29:20], c_addr[21:0]}
                                 : {hit_pte[29:10], c_addr[11:0]};

  // --------------------------------------------------------------- walker
  typedef enum logic [2:0] {IDLE, WALK1, WALK0, WRITE_AD, FAULT} state_e;
  state_e      st_q;
  logic [31:0] walk_base_q;   // current table base (physical)
  logic        walk_l1_q;     // current level is 1
  logic [31:0] walk_pte_q;    // leaf PTE being written back / filled
  logic        fault_acc_q;   // FAULT reports access fault, not page fault

  wire [9:0] walk_vpn = walk_l1_q ? vpn[19:10] : vpn[9:0];
  wire [31:0] pte_addr = walk_base_q + {20'b0, walk_vpn, 2'b00};

  // Decoded fields of the PTE on the bus.
  wire [31:0] pte    = m_rdata;
  wire pte_leaf      = pte[1] || pte[3];               // R or X
  wire pte_invalid   = !pte[0] || (!pte[1] && pte[2]); // !V, or W without R
  wire pte_bad_ptr   = pte[7] || pte[6] || pte[4];     // D/A/U on a pointer
  wire pte_misal_sp  = walk_l1_q && pte[19:10] != 10'b0;
  wire [31:0] pte_ad = pte | 32'h40 | (is_store ? 32'h80 : 32'h0);

  // ------------------------------------------------------------ datapath
  always_comb begin
    c_ready   = 1'b0;
    c_rdata   = m_rdata;
    c_err     = 1'b0;
    c_pgfault = 1'b0;
    m_valid   = 1'b0;
    m_addr    = c_addr;
    m_wdata   = c_wdata;
    m_wstrb   = c_wstrb;

    if (bare) begin
      m_valid = c_valid;
      c_ready = m_ready;
      c_err   = m_err;
    end else begin
      unique case (st_q)
        IDLE: begin
          if (c_valid && hit_usable) begin
            if (!hit_perm) begin
              c_ready   = 1'b1;   // fault completion, no bus access
              c_pgfault = 1'b1;
            end else begin
              m_valid = 1'b1;
              m_addr  = hit_pa;
              c_ready = m_ready;
              c_err   = m_err;
            end
          end
          // miss (or unusable A/D state): the walker starts next edge
        end
        WALK1, WALK0: begin
          m_valid = c_valid;
          m_addr  = pte_addr;
          m_wstrb = 4'b0000;
          m_wdata = 32'b0;
        end
        WRITE_AD: begin
          m_valid = c_valid;
          m_addr  = pte_addr;
          m_wstrb = 4'b1111;
          m_wdata = walk_pte_q;
        end
        default: begin  // FAULT
          c_ready   = 1'b1;
          c_err     = fault_acc_q;
          c_pgfault = !fault_acc_q;
        end
      endcase
    end
  end

  // ------------------------------------------------------------- control
  always_ff @(posedge clk) begin
    if (rst) begin
      st_q <= IDLE;
      fill_ptr <= '0;
      for (int i = 0; i < N; i++) tlb_v[i] <= 1'b0;
    end else begin
      unique case (st_q)
        IDLE: begin
          if (!bare && c_valid && !hit_usable && !flush) begin
            st_q        <= WALK1;
            walk_base_q <= {satp[19:0], 12'b0};
            walk_l1_q   <= 1'b1;
          end
        end

        WALK1, WALK0: begin
          if (bare || flush || !c_valid) begin
            // Context changed under the walk (trap/xret/serialized commit
            // redirect) or the request vanished: abandon without filling.
            // Our slaves resolve each cycle independently, so dropping a
            // waited-on PTE read is harmless.
            st_q <= IDLE;
          end else if (m_ready) begin
            if (m_err) begin
              st_q <= FAULT; fault_acc_q <= 1'b1;
            end else if (pte_invalid) begin
              st_q <= FAULT; fault_acc_q <= 1'b0;
            end else if (!pte_leaf) begin
              if (pte_bad_ptr || st_q == WALK0) begin
                st_q <= FAULT; fault_acc_q <= 1'b0;  // reserved / no leaf
              end else begin
                st_q        <= WALK0;
                walk_base_q <= {pte[29:10], 12'b0};
                walk_l1_q   <= 1'b0;
              end
            end else if (!perm_ok(pte[4:1]) || pte_misal_sp) begin
              st_q <= FAULT; fault_acc_q <= 1'b0;
            end else if (pte_ad != pte) begin
              st_q       <= WRITE_AD;
              walk_pte_q <= pte_ad;
            end else begin
              // Fill; the held request replays through the TLB next cycle.
              tlb_v[fill_ptr]     <= 1'b1;
              tlb_super[fill_ptr] <= walk_l1_q;
              tlb_vpn[fill_ptr]   <= vpn;
              tlb_pte[fill_ptr]   <= pte;
              fill_ptr            <= fill_ptr + 1'b1;
              st_q                <= IDLE;
            end
          end
        end

        WRITE_AD: begin
          if (bare || flush || !c_valid) begin
            st_q <= IDLE;        // A/D memory update is idempotent: safe
          end else if (m_ready) begin
            if (m_err) begin
              st_q <= FAULT; fault_acc_q <= 1'b1;
            end else begin
              tlb_v[fill_ptr]     <= 1'b1;
              tlb_super[fill_ptr] <= walk_l1_q;
              tlb_vpn[fill_ptr]   <= vpn;
              tlb_pte[fill_ptr]   <= walk_pte_q;
              fill_ptr            <= fill_ptr + 1'b1;
              st_q                <= IDLE;
            end
          end
        end

        default: st_q <= IDLE;   // FAULT: single-cycle completion
      endcase

      // Flush wins over a same-cycle fill: a serialized commit (satp write,
      // sfence.vma) must never leave a stale translation behind.
      if (flush) for (int i = 0; i < N; i++) tlb_v[i] <= 1'b0;
    end
  end

  // Unused slices: satp's ASID field; PTE software/global/valid bits and
  // the 34-bit-PA overflow bits, kept whole in the TLB for simplicity.
  // verilator lint_off UNUSED
  logic unused_bits;
  assign unused_bits = ^{satp[30:20], hit_pte[31:30], hit_pte[9:8],
                         hit_pte[5], hit_pte[0]};
  // verilator lint_on UNUSED

endmodule
