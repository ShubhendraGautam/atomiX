# Unit testbenches: one Verilator build+run per RTL module.
# `make` runs them all; `make run-<module>` runs one.
VERILATOR ?= verilator
VFLAGS    := -Wall --cc -O2

RTL_CORE := ../../rtl/core

MODULES := regfile alu immdec decoder branch_cmp muldiv

CORE_SVS := axcore_pkg regfile alu immdec decoder branch_cmp muldiv csr_file axcore

test: $(MODULES:%=run-%) run-axcore

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

clean:
	rm -rf obj_dir_* build

.PHONY: test clean run-axcore
