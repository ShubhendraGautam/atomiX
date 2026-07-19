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

PROGRAMS := hello timer preempt fencei spi sd role
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

clean:
	rm -rf build

.PHONY: all images run-iss run-qemu run-rtl check-hello check-timer check-preempt check-fencei check-spi check-sd check-role clean
