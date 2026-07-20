RISCV_PREFIX ?= riscv64-unknown-elf-
RISCV_ARCH   ?= rv32im
CC      := $(RISCV_PREFIX)gcc
OBJCOPY := $(RISCV_PREFIX)objcopy

CPPFLAGS := -Iinclude
# GCC 10 (Ubuntu 22.04) includes Zicsr in RV32IM but rejects the newer
# explicit `_zicsr` spelling. Override RISCV_ARCH for a newer toolchain.
CFLAGS   := -march=$(RISCV_ARCH) -mabi=ilp32 -mcmodel=medany -mno-relax \
            -msmall-data-limit=0 -ffreestanding -fno-builtin -fno-pic \
            -fno-stack-protector -Wall -Wextra -Werror -O2 -g
LDFLAGS  = -nostdlib -Wl,-T,link.ld -Wl,--gc-sections -Wl,-Map,$(@:.elf=.map)

PROGRAMS := hello timer preempt fencei spi sd role tpu gpu gpu_perf
ELFS     := $(PROGRAMS:%=build/%.elf)
BINS     := $(ELFS:.elf=.bin)
HEXS     := $(ELFS:.elf=.hex)

all: $(ELFS)
images: $(BINS) $(HEXS)

build/%.elf: crt0.S traps.S runtime.c examples/%.c link.ld include/platform.h include/csr.h include/clint.h include/role.h | build
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) -o $@ crt0.S traps.S runtime.c examples/$*.c

build/%.bin: build/%.elf
	$(OBJCOPY) -O binary $< $@

build/%.hex: build/%.bin ../../tests/directed/bin2hex.py
	python3 ../../tests/directed/bin2hex.py $< $@

build:
	mkdir -p $@

run-iss: build/hello.elf ../../sim/axsim/axsim
	../../sim/axsim/axsim --bin build/hello.elf

run-qemu: build/hello.elf
	timeout --foreground 10s qemu-system-riscv32 -machine virt -bios none \
	  -nographic -kernel $<

run-rtl: build/hello.hex
	$(MAKE) -C ../../sim/soc run RAM_INIT_FILE=$(abspath $<) RESET_PC=0x80000000

check-hello: images
	$(MAKE) -C ../../sim/axsim axsim
	python3 check_hello.py

check-timer: images
	$(MAKE) -C ../../sim/axsim axsim
	python3 check_timer.py

check-preempt: images
	$(MAKE) -C ../../sim/axsim axsim
	python3 check_preempt.py

check-fencei: images
	$(MAKE) -C ../../sim/axsim axsim
	python3 check_fencei.py

check-spi: images
	python3 check_spi.py

check-sd: images
	python3 check_sd.py

# RTL-only: the ISS does not model the role window.  The profile selects
# role.loopback; the program exits through the finisher, so a wrong result
# fails this target.
check-role: build/role.hex
	$(MAKE) -C ../../sim/soc run \
	  COMPONENT_CONFIG=../../configs/sim-role-loopback.json \
	  RAM_INIT_FILE=$(abspath build/role.hex) RESET_PC=0x80000000

# RTL-only like check-role.  The raised cycle budget covers the on-core
# reference matmuls (three 12x8x8 GEMMs on the 32-cycle mul/div unit).
check-tpu: build/tpu.hex
	$(MAKE) -C ../../sim/soc run \
	  COMPONENT_CONFIG=../../configs/sim-tpu-lite.json \
	  RAM_INIT_FILE=$(abspath build/tpu.hex) RESET_PC=0x80000000 MAX_CYCLES=400000

# RTL-only like check-tpu.  The raised cycle budget covers the on-core saxpy
# reference and interpreter oracle over the SIMT kernels.
check-gpu: build/gpu.hex
	$(MAKE) -C ../../sim/soc run \
	  COMPONENT_CONFIG=../../configs/sim-gpu-compute.json \
	  RAM_INIT_FILE=$(abspath build/gpu.hex) RESET_PC=0x80000000 MAX_CYCLES=400000

# GPU performance regression: sweeps thread counts over two kernels, times the
# SIMT engine against on-core loops, and fails if the measured speedup drops
# below the bar.  The raised cycle budget covers the sweep plus the per-thread
# MMIO uploads.  To measure a different core/lane count, point COMPONENT_CONFIG
# at your own profile (the reduced-lane and minimal-host components ship in the
# catalog; compose a profile when you need it).
check-gpu-perf: build/gpu_perf.hex
	$(MAKE) -C ../../sim/soc run \
	  COMPONENT_CONFIG=../../configs/sim-gpu-compute.json \
	  RAM_INIT_FILE=$(abspath build/gpu_perf.hex) RESET_PC=0x80000000 MAX_CYCLES=4000000

# Component test SUITE, not a per-hardware check: one target exercises the whole
# minimal-host family — core.minimal driving the CPU (hello), the GPU role, and
# the TPU role — so a lean-component variant is covered without a separate check
# and profile per combination.  Add a suite (and its sim-<name>-* fixtures) when
# a family of components warrants coverage together.
check-suite-minimal: build/hello.hex build/gpu.hex build/tpu.hex
	@echo "== suite/minimal: core.minimal + CPU (hello) =="
	$(MAKE) -C ../../sim/soc run COMPONENT_CONFIG=../../configs/sim-minimal.json \
	  RAM_INIT_FILE=$(abspath build/hello.hex) RESET_PC=0x80000000 MAX_CYCLES=200000
	@echo "== suite/minimal: core.minimal + GPU (gpu-compute-6) =="
	$(MAKE) -C ../../sim/soc run COMPONENT_CONFIG=../../configs/sim-minimal-gpu.json \
	  RAM_INIT_FILE=$(abspath build/gpu.hex) RESET_PC=0x80000000 MAX_CYCLES=600000
	@echo "== suite/minimal: core.minimal + TPU (tpu-lite) =="
	$(MAKE) -C ../../sim/soc run COMPONENT_CONFIG=../../configs/sim-minimal-tpu.json \
	  RAM_INIT_FILE=$(abspath build/tpu.hex) RESET_PC=0x80000000 MAX_CYCLES=400000
	@echo "suite/minimal: PASS"

clean:
	rm -rf build

.PHONY: all images run-iss run-qemu run-rtl check-hello check-timer check-preempt check-fencei check-spi check-sd check-role check-tpu check-gpu check-gpu-perf check-suite-minimal clean
