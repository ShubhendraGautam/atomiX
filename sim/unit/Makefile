# Unit testbenches: one Verilator build+run per RTL module.
# `make` runs them all; `make run-<module>` runs one.
VERILATOR ?= verilator
VFLAGS    := -Wall --cc -O2

RTL_CORE := ../../rtl/core
RTL_SOC  := ../../rtl/soc

MODULES := regfile alu immdec decoder branch_cmp muldiv

CORE_SVS := axcore_pkg regfile alu immdec decoder branch_cmp muldiv csr_file axcore
SOC_SVS := $(CORE_SVS:%=$(RTL_CORE)/%.sv) \
  $(RTL_SOC)/axbus_mux.sv $(RTL_SOC)/axram.sv $(RTL_SOC)/axrom.sv \
  $(RTL_SOC)/clint.sv $(RTL_SOC)/uart.sv $(RTL_SOC)/test_finisher.sv \
  $(RTL_SOC)/soc_top.sv

test: $(MODULES:%=run-%) run-axcore run-interrupt run-soc run-soc-timer

run-%: $(RTL_CORE)/%.sv tb_%.cpp
	$(VERILATOR) $(VFLAGS) --top-module $* --Mdir obj_dir_$* --exe --build \
	  -o tb_$* $(RTL_CORE)/axcore_pkg.sv $< $(CURDIR)/tb_$*.cpp >/dev/null
	./obj_dir_$*/tb_$*

# Core-level: run the directed test binaries on the RTL, with and without
# bus wait states (exercises the stall/hold paths).
run-axcore: $(CORE_SVS:%=$(RTL_CORE)/%.sv) tb_axcore.cpp
	$(VERILATOR) $(VFLAGS) --top-module axcore --Mdir obj_dir_axcore \
	  --exe --build -o tb_axcore \
	  $(CORE_SVS:%=$(RTL_CORE)/%.sv) $(CURDIR)/tb_axcore.cpp >/dev/null
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
run-interrupt: $(CORE_SVS:%=$(RTL_CORE)/%.sv) tb_interrupt.cpp
	$(VERILATOR) $(VFLAGS) --top-module axcore --Mdir obj_dir_interrupt \
	  --exe --build -o tb_interrupt \
	  $(CORE_SVS:%=$(RTL_CORE)/%.sv) $(CURDIR)/tb_interrupt.cpp >/dev/null
	./obj_dir_interrupt/tb_interrupt

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

.PHONY: test clean run-axcore run-interrupt run-soc run-soc-timer
