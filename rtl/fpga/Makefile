# Reproducible ULX3S-85F build.  The generated .bit configures SRAM only;
# `make flash` is deliberately a separate, explicit persistent operation.
YOSYS ?= yosys
NEXTPNR ?= nextpnr-ecp5
ECPPACK ?= ecppack
OPENFPGALOADER ?= openFPGALoader

TOP := ulx3s_85f_top
BUILD := build
BOOT_HEX := ../../sw/bootrom/build/bootrom.hex
LPF := ulx3s_85f.lpf
JSON := $(BUILD)/$(TOP).json
CONFIG := $(BUILD)/$(TOP).config
BIT := $(BUILD)/$(TOP).bit
SYNTH_SV := $(BUILD)/atomiX_synth.sv

RTL_CORE := ../core
RTL_SOC := ../soc
SOURCES := \
  $(RTL_CORE)/axcore_pkg.sv $(RTL_CORE)/regfile.sv $(RTL_CORE)/alu.sv \
  $(RTL_CORE)/immdec.sv $(RTL_CORE)/decoder.sv $(RTL_CORE)/branch_cmp.sv \
  $(RTL_CORE)/muldiv.sv $(RTL_CORE)/csr_file.sv $(RTL_CORE)/sv32_mmu.sv \
  $(RTL_CORE)/axcore.sv \
  $(RTL_SOC)/axbus_mux.sv $(RTL_SOC)/axcache.sv $(RTL_SOC)/axram.sv \
  $(RTL_SOC)/axdram_model.sv $(RTL_SOC)/axsdram.sv $(RTL_SOC)/axrom.sv \
  $(RTL_SOC)/clint.sv $(RTL_SOC)/uart.sv $(RTL_SOC)/axspi.sv \
  $(RTL_SOC)/test_finisher.sv $(RTL_SOC)/soc_top.sv \
  axuart_phy.sv $(TOP).sv

all: check-tools $(BIT)

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

$(SYNTH_SV): prepare_synth.py $(SOURCES) | $(BUILD)
	python3 prepare_synth.py $@ $(SOURCES)

$(JSON): $(SYNTH_SV) $(BOOT_HEX) Makefile | $(BUILD)
	$(YOSYS) -q -l $(BUILD)/synth.log -p 'read_verilog -lib +/ecp5/cells_sim.v; read_verilog -lib +/ecp5/cells_bb.v; read_verilog -sv $(SYNTH_SV); hierarchy -check -top $(TOP); synth_ecp5 -noabc9 -top $(TOP) -json $@'

$(CONFIG): $(JSON) $(LPF) | check-tools
	$(NEXTPNR) --85k --package CABGA381 --speed 6 --freq 25 \
	  --json $(JSON) --lpf $(LPF) --textcfg $@

$(BIT): $(CONFIG)
	$(ECPPACK) --idcode 0x41113043 $(CONFIG) $@

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

.PHONY: all check-tools toolchain-report program flash clean
