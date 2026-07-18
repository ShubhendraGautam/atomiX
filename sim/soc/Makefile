VERILATOR ?= verilator
VFLAGS    := -Wall --cc -O2
.DEFAULT_GOAL := run

COMPONENT_CONFIG ?= ../../configs/sim-bram.json
# Keep one generated include per absolute profile path.  This avoids retaining
# a stale selection when two DIY worktrees both contain `sim-bram.json`.
COMPONENT_CONFIG_KEY := $(subst /,_,$(basename $(abspath $(COMPONENT_CONFIG))))
COMPONENT_MK := build/component$(COMPONENT_CONFIG_KEY).mk
COMPONENT_MANIFESTS := $(wildcard ../../components/*/*/component.json)

$(COMPONENT_MK): $(COMPONENT_CONFIG) ../../tools/configure.py $(COMPONENT_MANIFESTS)
	mkdir -p $(@D)
	python3 ../../tools/configure.py resolve --config $(COMPONENT_CONFIG) --output $@

-include $(COMPONENT_MK)

# The resolver learns external manifest paths from the configuration itself.
# Once included, make tracks them too, so editing an out-of-tree manifest
# regenerates the selected build without touching the profile JSON.
$(COMPONENT_MK): $(COMPONENT_SELECTED_MANIFESTS)

RTL_CORE := ../../rtl/core
RTL_SOC  := ../../rtl/soc
SOC_SVS  := $(COMPONENT_CORE_SOURCES) \
  $(RTL_SOC)/axbus_mux.sv $(RTL_SOC)/axcache.sv $(RTL_SOC)/axram.sv \
  $(RTL_SOC)/axdram_model.sv $(RTL_SOC)/axsdram.sv \
  $(RTL_SOC)/axmem_reference.sv $(COMPONENT_MEMORY_SOURCES) \
  $(RTL_SOC)/axrom.sv $(COMPONENT_CLINT_SOURCES) \
  $(COMPONENT_UART_SOURCES) $(COMPONENT_SPI_SOURCES) \
  $(RTL_SOC)/test_finisher.sv $(COMPONENT_SOC_SOURCES)

# Caller supplies a word-per-line $readmemh image and may choose its entry.
RAM_INIT_FILE ?=
ROM_INIT_FILE ?=
RESET_PC ?= $(COMPONENT_RESET_PC)
RAM_BYTES ?= $(COMPONENT_RAM_BYTES)
EXTERNAL_MEMORY ?= $(COMPONENT_USE_DRAM_MODEL)
CACHES ?= $(COMPONENT_CACHES)
MAX_CYCLES ?= 100000
SD_IMAGE ?=
BUILD_ID ?= default
UART_INPUT ?=
UART_INPUT_FILE ?=

# The RAM image and elaborated memory/cache parameters are compiled into the
# Verilator model. Keep configurations separate so one run cannot accidentally
# execute a binary built for a different image.
BUILD_TAG := $(COMPONENT_CONFIG_NAME)-$(notdir $(RAM_INIT_FILE))-$(notdir $(ROM_INIT_FILE))-ram$(RAM_BYTES)-dram$(EXTERNAL_MEMORY)-cache$(CACHES)-$(BUILD_ID)
MDIR := obj_dir_$(BUILD_TAG)

config: $(COMPONENT_MK)
	@echo "configuration: $(COMPONENT_CONFIG_NAME)"
	@echo "  core:   $(COMPONENT_CORE_ID)"
	@echo "  soc:    $(COMPONENT_SOC_ID)"
	@echo "  memory: $(COMPONENT_MEMORY_ID)"
	@echo "  board:  $(COMPONENT_BOARD_ID)"

# Run the harness selected by the memory component.  `run` and `run-sdram`
# remain available for direct, explicit experiments and backwards compatibility.
run-config: $(COMPONENT_MK)
	@case "$(COMPONENT_SIM_RUNNER)" in run|run-sdram) ;; *) \
	  echo "unsupported simulation runner: $(COMPONENT_SIM_RUNNER)"; exit 2;; esac
	$(MAKE) $(COMPONENT_SIM_RUNNER)

run: $(SOC_SVS) tb_soc.cpp
	@test -n "$(RAM_INIT_FILE)" || (echo "RAM_INIT_FILE is required"; exit 2)
	$(VERILATOR) $(VFLAGS) --top-module soc_top --Mdir $(MDIR) --exe --build \
	  -GRESET_PC=$(RESET_PC) -GRAM_BYTES=$(RAM_BYTES) -GUSE_DRAM_MODEL=$(EXTERNAL_MEMORY) -GUSE_CACHES=$(CACHES) \
	  -GRAM_INIT_FILE=\"$(RAM_INIT_FILE)\" -GROM_INIT_FILE=\"$(ROM_INIT_FILE)\" \
	  -o axsoc $(SOC_SVS) $(CURDIR)/tb_soc.cpp >/dev/null
	./$(MDIR)/axsoc --max-cycles $(MAX_CYCLES) --uart-input '$(UART_INPUT)' $(if $(UART_INPUT_FILE),--uart-input-file "$(UART_INPUT_FILE)",) $(if $(SD_IMAGE),--sd-image "$(SD_IMAGE)",)

# Full Phase-6 controller integration: the SoC uses axsdram rather than the
# delayed array model, and a CAS-2 x16 SDRAM model sits at the physical pins.
run-sdram: $(SOC_SVS) ax_sdram_sim.sv soc_sdram_test_top.sv tb_soc_sdram.cpp
	@test -n "$(ROM_INIT_FILE)" || (echo "ROM_INIT_FILE is required"; exit 2)
	$(VERILATOR) $(VFLAGS) --top-module soc_sdram_test_top --Mdir $(MDIR)-sdram --exe --build \
	  -GROM_INIT_FILE=\"$(ROM_INIT_FILE)\" -o axsoc \
	  $(SOC_SVS) $(CURDIR)/ax_sdram_sim.sv $(CURDIR)/soc_sdram_test_top.sv \
	  $(CURDIR)/tb_soc_sdram.cpp >/dev/null
	./$(MDIR)-sdram/axsoc --max-cycles $(MAX_CYCLES) --uart-input '$(UART_INPUT)' $(if $(UART_INPUT_FILE),--uart-input-file "$(UART_INPUT_FILE)",) $(if $(SD_IMAGE),--sd-image "$(SD_IMAGE)",)

clean:
	rm -rf obj_dir build

.PHONY: config run run-sdram run-config clean
