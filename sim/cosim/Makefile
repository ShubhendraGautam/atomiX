# Lock-step ISS/RTL cosimulation.  The harness links aXsim directly into the
# Verilator executable, so it compares each architectural event without any
# file-based trace parsing.
VERILATOR ?= verilator
VFLAGS    := -Wall --cc -O2

CORE_COMPONENT := ../../components/core/pipeline5
AXSIM     := ../axsim/src
CORE_SVS  := axcore_pkg regfile alu immdec decoder branch_cmp muldiv csr_file sv32_mmu axcore
CORE_SOURCES := $(CORE_SVS:%=$(CORE_COMPONENT)/%.sv)

axcosim: $(CORE_SOURCES) tb_cosim.cpp $(AXSIM)/bus.h \
         $(AXSIM)/cpu.h $(AXSIM)/cpu.cpp $(AXSIM)/elf.h $(AXSIM)/elf.cpp
	$(VERILATOR) $(VFLAGS) --top-module axcore --Mdir obj_dir --exe --build \
	  -o axcosim $(CORE_SOURCES) tb_cosim.cpp \
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

M_TESTS := rv32um-p-div rv32um-p-divu rv32um-p-mul rv32um-p-mulh \
           rv32um-p-mulhsu rv32um-p-mulhu rv32um-p-rem rv32um-p-remu

rv32um: axcosim
	$(MAKE) -C ../../tests/riscv-tests/isa XLEN=32 RISCV_PREFIX=riscv64-unknown-elf- $(M_TESTS)
	SIM=$(CURDIR)/obj_dir/axcosim ../../tests/run-riscv-tests.sh rv32um

clean:
	rm -rf obj_dir build

.PHONY: test riscv-tests rv32um clean
