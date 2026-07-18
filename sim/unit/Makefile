# Unit testbenches: one Verilator build+run per RTL module.
# `make` runs them all; `make run-<module>` runs one.
VERILATOR ?= verilator
VFLAGS    := -Wall --cc -O2

CORE_COMPONENT := ../../components/core/pipeline5
COMPONENTS := ../../components
MEMORY_REFERENCE := $(COMPONENTS)/memory/reference
INTERCONNECT_REFERENCE := $(COMPONENTS)/interconnect/axbus-reference
CACHE_REFERENCE := $(COMPONENTS)/cache/direct-mapped
ROM_REFERENCE := $(COMPONENTS)/rom/axrom
CLINT_REFERENCE := $(COMPONENTS)/clint/qemu_virt
UART_REFERENCE := $(COMPONENTS)/uart/mmio16550
SPI_REFERENCE := $(COMPONENTS)/spi/polling_mode0
FINISHER_REFERENCE := $(COMPONENTS)/finisher/sifive-test
SOC_REFERENCE := $(COMPONENTS)/soc/reference
HARNESS_SDRAM := $(COMPONENTS)/harness/verilator-sdram
BOARD_ULX3S := $(COMPONENTS)/board/ulx3s_85f

MODULES := regfile alu immdec decoder branch_cmp muldiv

CORE_SVS := axcore_pkg regfile alu immdec decoder branch_cmp muldiv csr_file sv32_mmu axcore
CORE_SOURCES := $(CORE_SVS:%=$(CORE_COMPONENT)/%.sv)
MEMORY_SOURCES := $(MEMORY_REFERENCE)/axram.sv $(MEMORY_REFERENCE)/axdram_model.sv \
  $(MEMORY_REFERENCE)/axsdram.sv $(MEMORY_REFERENCE)/axmem_reference.sv \
  $(MEMORY_REFERENCE)/axmem.sv
SOC_SVS := $(CORE_SOURCES) \
  $(INTERCONNECT_REFERENCE)/axbus_mux.sv $(CACHE_REFERENCE)/axcache.sv \
  $(MEMORY_SOURCES) $(ROM_REFERENCE)/axrom.sv \
  $(CLINT_REFERENCE)/clint.sv $(UART_REFERENCE)/uart.sv \
  $(SPI_REFERENCE)/axspi.sv $(FINISHER_REFERENCE)/test_finisher.sv \
  $(SOC_REFERENCE)/soc_top.sv

test: $(MODULES:%=run-%) run-axcore run-interrupt run-irq-collide run-privilege run-sv32 run-axdram-model run-axcache run-axsdram run-axuart-phy run-axspi run-soc run-soc-timer

run-%: $(CORE_COMPONENT)/%.sv tb_%.cpp
	$(VERILATOR) $(VFLAGS) --top-module $* --Mdir obj_dir_$* --exe --build \
	  -o tb_$* $(CORE_COMPONENT)/axcore_pkg.sv $< $(CURDIR)/tb_$*.cpp >/dev/null
	./obj_dir_$*/tb_$*

# Core-level: run the directed test binaries on the RTL, with and without
# bus wait states (exercises the stall/hold paths).
run-axcore: $(CORE_SOURCES) tb_axcore.cpp
	$(VERILATOR) $(VFLAGS) --top-module axcore --Mdir obj_dir_axcore \
	  --exe --build -o tb_axcore \
	  $(CORE_SOURCES) $(CURDIR)/tb_axcore.cpp >/dev/null
	@mkdir -p build
	python3 ../../tests/directed/words2bin.py ../../tests/directed/smoke_ok.words build/smoke_ok.bin
	python3 ../../tests/directed/words2bin.py ../../tests/directed/trap_ecall.words build/trap_ecall.bin
	./obj_dir_axcore/tb_axcore build/smoke_ok.bin | grep -qx OK
	./obj_dir_axcore/tb_axcore build/smoke_ok.bin --ws 3 | grep -qx OK
	./obj_dir_axcore/tb_axcore build/trap_ecall.bin
	./obj_dir_axcore/tb_axcore build/trap_ecall.bin --ws 2
	@echo "tb_axcore: PASS"

# Timer line asserted before MTIE/MIE are enabled. The handler's finisher
# write proves precise delivery and the C++ test checks mcause/mepc/mip.
run-interrupt: $(CORE_SOURCES) tb_interrupt.cpp
	$(VERILATOR) $(VFLAGS) --top-module axcore --Mdir obj_dir_interrupt \
	  --exe --build -o tb_interrupt \
	  $(CORE_SOURCES) $(CURDIR)/tb_interrupt.cpp >/dev/null
	./obj_dir_interrupt/tb_interrupt

# Interrupt/commit collision regressions: a timer interrupt forced onto the
# exact commit cycle of a serialized CSR write and of an mret. The .S
# programs self-check mepc/mstatus/mscratch and exit via the finisher.
RISCV_PREFIX ?= riscv64-unknown-elf-
IRQ_TESTS := irq_csr irq_mret
IRQ_MARKER_irq_csr  := 34011073   # csrw mscratch, x2
IRQ_MARKER_irq_mret := 30200073   # mret

run-irq-collide: $(CORE_SOURCES) tb_irq_collide.cpp
	$(VERILATOR) $(VFLAGS) --top-module axcore --Mdir obj_dir_irq_collide \
	  --exe --build -o tb_irq_collide \
	  $(CORE_SOURCES) $(CURDIR)/tb_irq_collide.cpp >/dev/null
	@mkdir -p build
	for t in $(IRQ_TESTS); do \
	  $(RISCV_PREFIX)gcc -march=rv32i -mabi=ilp32 -nostdlib -nostartfiles \
	    -Wl,-T,../../sw/baremetal/link.ld -o build/$$t.elf \
	    ../../tests/directed/$$t.S || exit 1; \
	  $(RISCV_PREFIX)objcopy -O binary build/$$t.elf build/$$t.bin || exit 1; \
	done
	./obj_dir_irq_collide/tb_irq_collide build/irq_csr.bin $(IRQ_MARKER_irq_csr)
	./obj_dir_irq_collide/tb_irq_collide build/irq_mret.bin $(IRQ_MARKER_irq_mret)

# M/S/U transition and trap-routing regression.  The directed program is
# self-checking and uses the same behavioral aXbus testbench as run-axcore.
run-privilege: $(CORE_SOURCES) tb_axcore.cpp ../../tests/directed/privilege.S
	$(VERILATOR) $(VFLAGS) --top-module axcore --Mdir obj_dir_privilege \
	  --exe --build -o tb_axcore \
	  $(CORE_SOURCES) $(CURDIR)/tb_axcore.cpp >/dev/null
	@mkdir -p build
	$(RISCV_PREFIX)gcc -march=rv32i -mabi=ilp32 -nostdlib -nostartfiles \
	  -Wl,-T,../../sw/baremetal/link.ld -o build/privilege.elf \
	  ../../tests/directed/privilege.S
	$(RISCV_PREFIX)objcopy -O binary build/privilege.elf build/privilege.bin
	./obj_dir_privilege/tb_axcore build/privilege.bin
	./obj_dir_privilege/tb_axcore build/privilege.bin --ws 2
	@echo "tb_privilege: PASS"

# End-to-end Sv32 execution: M-mode page-table setup, U-mode fetch/data
# walks, hardware A/D updates, and a precise store-page-fault trap.
run-sv32: $(CORE_SOURCES) tb_axcore.cpp ../../tests/directed/sv32.S
	$(VERILATOR) $(VFLAGS) --top-module axcore --Mdir obj_dir_sv32 \
	  --exe --build -o tb_axcore \
	  $(CORE_SOURCES) $(CURDIR)/tb_axcore.cpp >/dev/null
	@mkdir -p build
	$(RISCV_PREFIX)gcc -march=rv32i -mabi=ilp32 -nostdlib -nostartfiles \
	  -Wl,-T,../../sw/baremetal/link.ld -o build/sv32.elf \
	  ../../tests/directed/sv32.S
	$(RISCV_PREFIX)objcopy -O binary build/sv32.elf build/sv32.bin
	./obj_dir_sv32/tb_axcore build/sv32.bin
	./obj_dir_sv32/tb_axcore build/sv32.bin --ws 2
	@echo "tb_sv32: PASS"

# Phase 6 memory contract: both ports incur a fixed delayed response, honor
# byte lanes, and report bad requests only at completion.
run-axdram-model: $(MEMORY_REFERENCE)/axdram_model.sv tb_axdram_model.cpp
	$(VERILATOR) $(VFLAGS) --top-module axdram_model --Mdir obj_dir_axdram_model \
	  -GBYTES=64 -GLATENCY=3 --exe --build -o tb_axdram_model \
	  $< $(CURDIR)/tb_axdram_model.cpp >/dev/null
	./obj_dir_axdram_model/tb_axdram_model

# Physical-SDRAM controller contract against a CAS-2 x16 SDRAM behavioural
# model, including init, bank/row/column sequencing, byte masks, and faults.
run-axsdram: $(MEMORY_REFERENCE)/axsdram.sv $(HARNESS_SDRAM)/ax_sdram_sim.sv axsdram_test_top.sv tb_axsdram.cpp
	$(VERILATOR) $(VFLAGS) --top-module axsdram_test_top --Mdir obj_dir_axsdram \
	  --exe --build -o tb_axsdram $(MEMORY_REFERENCE)/axsdram.sv $(HARNESS_SDRAM)/ax_sdram_sim.sv $(CURDIR)/axsdram_test_top.sv \
	  $(CURDIR)/tb_axsdram.cpp >/dev/null
	./obj_dir_axsdram/tb_axsdram

run-axuart-phy: $(BOARD_ULX3S)/axuart_phy.sv axuart_phy_loopback_top.sv tb_axuart_phy.cpp
	$(VERILATOR) $(VFLAGS) --top-module axuart_phy_loopback_top --Mdir obj_dir_axuart_phy \
	  --exe --build -o tb_axuart_phy $^ >/dev/null
	./obj_dir_axuart_phy/tb_axuart_phy

run-axcache: $(CACHE_REFERENCE)/axcache.sv $(MEMORY_REFERENCE)/axdram_model.sv axcache_test_top.sv tb_axcache.cpp
	$(VERILATOR) $(VFLAGS) --top-module axcache_test_top --Mdir obj_dir_axcache \
	  --exe --build -o tb_axcache $^ >/dev/null
	./obj_dir_axcache/tb_axcache

run-axspi: $(SPI_REFERENCE)/axspi.sv tb_axspi.cpp
	$(VERILATOR) $(VFLAGS) --top-module axspi --Mdir obj_dir_axspi \
	  --exe --build -o tb_axspi $^ >/dev/null
	./obj_dir_axspi/tb_axspi

run-soc: $(SOC_SVS) tb_soc.cpp data/soc_smoke.hex
	$(VERILATOR) $(VFLAGS) --top-module soc_top --Mdir obj_dir_soc \
	  -GROM_INIT_FILE=\"$(CURDIR)/data/soc_smoke.hex\" --exe --build -o tb_soc \
	  $(SOC_SVS) $(CURDIR)/tb_soc.cpp >/dev/null
	./obj_dir_soc/tb_soc

run-soc-timer: $(SOC_SVS) tb_soc_timer.cpp data/soc_timer.hex
	$(VERILATOR) $(VFLAGS) --top-module soc_top --Mdir obj_dir_soc_timer \
	  -GROM_INIT_FILE=\"$(CURDIR)/data/soc_timer.hex\" --exe --build -o tb_soc_timer \
	  $(SOC_SVS) $(CURDIR)/tb_soc_timer.cpp >/dev/null
	./obj_dir_soc_timer/tb_soc_timer

clean:
	rm -rf obj_dir_* build

.PHONY: test clean run-axcore run-interrupt run-irq-collide run-privilege run-sv32 run-axdram-model run-axcache run-axsdram run-axuart-phy run-axspi run-soc run-soc-timer
