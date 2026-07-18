VERILATOR ?= verilator
VFLAGS    := -Wall --cc -O2

RTL_CORE := ../../rtl/core
RTL_SOC  := ../../rtl/soc
SOC_SVS  := $(RTL_CORE)/axcore_pkg.sv $(RTL_CORE)/regfile.sv \
  $(RTL_CORE)/alu.sv $(RTL_CORE)/immdec.sv $(RTL_CORE)/decoder.sv \
  $(RTL_CORE)/branch_cmp.sv $(RTL_CORE)/muldiv.sv $(RTL_CORE)/csr_file.sv \
  $(RTL_CORE)/sv32_mmu.sv $(RTL_CORE)/axcore.sv \
  $(RTL_SOC)/axbus_mux.sv $(RTL_SOC)/axram.sv \
  $(RTL_SOC)/axrom.sv $(RTL_SOC)/clint.sv $(RTL_SOC)/uart.sv \
  $(RTL_SOC)/test_finisher.sv $(RTL_SOC)/soc_top.sv

# Caller supplies a word-per-line $readmemh image and may choose its entry.
RAM_INIT_FILE ?=
RESET_PC ?= 0x80000000
UART_INPUT ?=
UART_INPUT_FILE ?=

run: $(SOC_SVS) tb_soc.cpp
	@test -n "$(RAM_INIT_FILE)" || (echo "RAM_INIT_FILE is required"; exit 2)
	$(VERILATOR) $(VFLAGS) --top-module soc_top --Mdir obj_dir --exe --build \
	  -GRESET_PC=$(RESET_PC) -GRAM_INIT_FILE=\"$(RAM_INIT_FILE)\" \
	  -o axsoc $(SOC_SVS) $(CURDIR)/tb_soc.cpp >/dev/null
	./obj_dir/axsoc --uart-input '$(UART_INPUT)' $(if $(UART_INPUT_FILE),--uart-input-file "$(UART_INPUT_FILE)",)

clean:
	rm -rf obj_dir

.PHONY: run clean
