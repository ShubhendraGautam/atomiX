VERILATOR ?= verilator
VFLAGS    := -Wall --cc -O2

RTL_CORE := ../../rtl/core
RTL_SOC  := ../../rtl/soc
SOC_SVS  := $(RTL_CORE)/axcore_pkg.sv $(RTL_CORE)/regfile.sv \
  $(RTL_CORE)/alu.sv $(RTL_CORE)/immdec.sv $(RTL_CORE)/decoder.sv \
  $(RTL_CORE)/branch_cmp.sv $(RTL_CORE)/muldiv.sv $(RTL_CORE)/csr_file.sv \
  $(RTL_CORE)/sv32_mmu.sv $(RTL_CORE)/axcore.sv \
  $(RTL_SOC)/axbus_mux.sv $(RTL_SOC)/axcache.sv $(RTL_SOC)/axram.sv $(RTL_SOC)/axdram_model.sv \
  $(RTL_SOC)/axrom.sv $(RTL_SOC)/clint.sv $(RTL_SOC)/uart.sv $(RTL_SOC)/axspi.sv \
  $(RTL_SOC)/test_finisher.sv $(RTL_SOC)/soc_top.sv

# Caller supplies a word-per-line $readmemh image and may choose its entry.
RAM_INIT_FILE ?=
RESET_PC ?= 0x80000000
RAM_BYTES ?= 131072
EXTERNAL_MEMORY ?= 0
CACHES ?= 0
MAX_CYCLES ?= 100000
SD_IMAGE ?=
UART_INPUT ?=
UART_INPUT_FILE ?=

# The RAM image and elaborated memory/cache parameters are compiled into the
# Verilator model. Keep configurations separate so one run cannot accidentally
# execute a binary built for a different image.
BUILD_TAG := $(notdir $(RAM_INIT_FILE))-ram$(RAM_BYTES)-dram$(EXTERNAL_MEMORY)-cache$(CACHES)
MDIR := obj_dir_$(BUILD_TAG)

run: $(SOC_SVS) tb_soc.cpp
	@test -n "$(RAM_INIT_FILE)" || (echo "RAM_INIT_FILE is required"; exit 2)
	$(VERILATOR) $(VFLAGS) --top-module soc_top --Mdir $(MDIR) --exe --build \
	  -GRESET_PC=$(RESET_PC) -GRAM_BYTES=$(RAM_BYTES) -GUSE_DRAM_MODEL=$(EXTERNAL_MEMORY) -GUSE_CACHES=$(CACHES) \
	  -GRAM_INIT_FILE=\"$(RAM_INIT_FILE)\" \
	  -o axsoc $(SOC_SVS) $(CURDIR)/tb_soc.cpp >/dev/null
	./$(MDIR)/axsoc --max-cycles $(MAX_CYCLES) --uart-input '$(UART_INPUT)' $(if $(UART_INPUT_FILE),--uart-input-file "$(UART_INPUT_FILE)",) $(if $(SD_IMAGE),--sd-image "$(SD_IMAGE)",)

clean:
	rm -rf obj_dir

.PHONY: run clean
