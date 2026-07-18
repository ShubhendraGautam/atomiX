# Lock-step ISS/RTL cosimulation.  The harness links aXsim directly into the
# Verilator executable, so it compares each architectural event without any
# file-based trace parsing.
VERILATOR ?= verilator
VFLAGS    := -Wall --cc -O2

RTL_CORE  := ../../rtl/core
AXSIM     := ../axsim/src
CORE_SVS  := axcore_pkg regfile alu immdec decoder branch_cmp csr_file axcore

axcosim: $(CORE_SVS:%=$(RTL_CORE)/%.sv) tb_cosim.cpp $(AXSIM)/bus.h \
         $(AXSIM)/cpu.h $(AXSIM)/cpu.cpp $(AXSIM)/elf.h $(AXSIM)/elf.cpp
	$(VERILATOR) $(VFLAGS) --top-module axcore --Mdir obj_dir --exe --build \
	  -o axcosim $(CORE_SVS:%=$(RTL_CORE)/%.sv) tb_cosim.cpp \
	  $(AXSIM)/cpu.cpp $(AXSIM)/elf.cpp -CFLAGS '-I$(abspath $(AXSIM))'

test: axcosim
	@mkdir -p build
	python3 ../../tests/directed/words2bin.py ../../tests/directed/smoke_ok.words build/smoke_ok.bin
	python3 ../../tests/directed/words2bin.py ../../tests/directed/trap_ecall.words build/trap_ecall.bin
	./obj_dir/axcosim --bin build/smoke_ok.bin
	./obj_dir/axcosim --bin build/smoke_ok.bin --ws 3
	./obj_dir/axcosim --bin build/trap_ecall.bin
	./obj_dir/axcosim --bin build/trap_ecall.bin --ws 2
	@echo "cosim directed tests: PASS"

# The ISA binaries must be built first as documented in tests/README.md.
riscv-tests: axcosim
	SIM=$(CURDIR)/obj_dir/axcosim ../../tests/run-riscv-tests.sh

clean:
	rm -rf obj_dir build

.PHONY: test riscv-tests clean
