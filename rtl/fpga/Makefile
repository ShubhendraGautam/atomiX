# Reproducible ULX3S-85F build.  The generated .bit configures SRAM only;
# `make flash` is deliberately a separate, explicit persistent operation.
YOSYS ?= yosys
NEXTPNR ?= nextpnr-ecp5
ECPPACK ?= ecppack
OPENFPGALOADER ?= openFPGALoader
.DEFAULT_GOAL := all

BUILD := build
COMPONENT_CONFIG ?= ../../configs/ulx3s-85f.json
COMPONENT_CONFIG_KEY := $(subst /,_,$(basename $(abspath $(COMPONENT_CONFIG))))
COMPONENT_MK := $(BUILD)/component$(COMPONENT_CONFIG_KEY).mk
COMPONENT_MANIFESTS := $(wildcard ../../components/*/*/component.json)

$(COMPONENT_MK): $(COMPONENT_CONFIG) ../../tools/configure.py $(COMPONENT_MANIFESTS)
	mkdir -p $(@D)
	python3 ../../tools/configure.py resolve --config $(COMPONENT_CONFIG) --output $@

-include $(COMPONENT_MK)

$(COMPONENT_MK): $(COMPONENT_SELECTED_MANIFESTS)

TOP ?= $(COMPONENT_FPGA_TOP)
BOOT_HEX := ../../sw/bootrom/build/bootrom.hex
LPF ?= $(COMPONENT_FPGA_LPF)
JSON := $(BUILD)/$(TOP).json
TEXTCFG := $(BUILD)/$(TOP).config
BIT := $(BUILD)/$(TOP).bit
SYNTH_SV := $(BUILD)/atomiX_synth.sv

RTL_CORE := ../core
RTL_SOC := ../soc
SOURCES := $(COMPONENT_CORE_SOURCES) \
  $(COMPONENT_INTERCONNECT_SOURCES) $(COMPONENT_CACHE_SOURCES) $(RTL_SOC)/axram.sv \
  $(RTL_SOC)/axdram_model.sv $(RTL_SOC)/axsdram.sv \
  $(RTL_SOC)/axmem_reference.sv $(COMPONENT_MEMORY_SOURCES) \
  $(COMPONENT_ROM_SOURCES) $(COMPONENT_CLINT_SOURCES) \
  $(COMPONENT_UART_SOURCES) $(COMPONENT_SPI_SOURCES) \
  $(COMPONENT_FINISHER_SOURCES) $(COMPONENT_SOC_SOURCES) \
  $(COMPONENT_BOARD_SOURCES)

all: check-tools $(BIT)

config: $(COMPONENT_MK)
	@echo "configuration: $(COMPONENT_CONFIG_NAME)"
	@echo "  core:   $(COMPONENT_CORE_ID)"
	@echo "  soc:    $(COMPONENT_SOC_ID)"
	@echo "  memory: $(COMPONENT_MEMORY_ID)"
	@echo "  interconnect: $(COMPONENT_INTERCONNECT_ID)"
	@echo "  cache:  $(COMPONENT_CACHE_ID)"
	@echo "  rom:    $(COMPONENT_ROM_ID)"
	@echo "  board:  $(COMPONENT_BOARD_ID)"

check-tools:
	@command -v $(YOSYS) >/dev/null || { echo "missing: $(YOSYS)"; exit 2; }
	@command -v $(NEXTPNR) >/dev/null || { echo "missing: $(NEXTPNR)"; exit 2; }
	@command -v $(ECPPACK) >/dev/null || { echo "missing: $(ECPPACK)"; exit 2; }

toolchain-report: check-tools
	@$(YOSYS) -V
	@$(NEXTPNR) --version
	@$(ECPPACK) --version
	@if command -v $(OPENFPGALOADER) >/dev/null; then $(OPENFPGALOADER) --version; else echo "openFPGALoader: not found (programming only)"; fi

$(BOOT_HEX):
	$(MAKE) -C ../../sw/bootrom images

$(SYNTH_SV): prepare_synth.py $(COMPONENT_MK) $(SOURCES) | $(BUILD)
	python3 prepare_synth.py $@ $(SOURCES)

$(JSON): $(SYNTH_SV) $(BOOT_HEX) Makefile | $(BUILD)
	$(YOSYS) -q -l $(BUILD)/synth.log -p 'read_verilog -lib +/ecp5/cells_sim.v; read_verilog -lib +/ecp5/cells_bb.v; read_verilog -sv $(SYNTH_SV); hierarchy -check -top $(TOP); synth_ecp5 -noabc9 -top $(TOP) -json $@'

$(TEXTCFG): $(JSON) $(LPF) | check-tools
	$(NEXTPNR) $(COMPONENT_NEXTPNR_ARGS) --json $(JSON) --lpf $(LPF) --textcfg $@

$(BIT): $(TEXTCFG)
	$(ECPPACK) $(COMPONENT_ECPPACK_ARGS) $(TEXTCFG) $@

$(BUILD):
	mkdir -p $@

# Reversible SRAM configuration. It is the only programming command needed
# for bring-up; power-cycling restores the board's flash-loaded image.
program: $(BIT)
	@command -v $(OPENFPGALOADER) >/dev/null || { echo "missing: $(OPENFPGALOADER)"; exit 2; }
	$(OPENFPGALOADER) -b ulx3s $(BIT)

# Persistent configuration flash write. Run only after `program` and the
# console/SDRAM regression have passed on the physical board.
flash: $(BIT)
	@command -v $(OPENFPGALOADER) >/dev/null || { echo "missing: $(OPENFPGALOADER)"; exit 2; }
	$(OPENFPGALOADER) -b ulx3s -f $(BIT)

clean:
	rm -rf $(BUILD)

.PHONY: all config check-tools toolchain-report program flash clean
