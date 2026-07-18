RISCV_PREFIX ?= riscv64-unknown-elf-
RISCV_ARCH   ?= rv32im
CC      := $(RISCV_PREFIX)gcc
OBJCOPY := $(RISCV_PREFIX)objcopy
QEMU    ?= qemu-system-riscv32
export QEMU

CPPFLAGS := -Iinclude
# GCC 10's ISA parser accepts rv32im (which includes CSR instructions) but
# rejects the newer explicit _zicsr spelling. Newer toolchains may override
# RISCV_ARCH without changing the source.
CFLAGS := -march=$(RISCV_ARCH) -mabi=ilp32 -mcmodel=medany -mno-relax \
          -msmall-data-limit=0 -ffreestanding -fno-builtin -fno-pic \
          -fno-stack-protector -Wall -Wextra -Werror -O2 -g
LDFLAGS = -nostdlib -Wl,-T,link.ld -Wl,--gc-sections -Wl,-Map,$(@:.elf=.map)

ELF := build/axos_boot.elf
BIN := $(ELF:.elf=.bin)
HEX := $(ELF:.elf=.hex)
SOURCES := boot.S trap.S user.S kernel.c page.c shell.c

all: $(ELF)
images: $(BIN) $(HEX)

$(ELF): $(SOURCES) link.ld include/platform.h include/page.h | build
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) -o $@ $(SOURCES)

$(BIN): $(ELF)
	$(OBJCOPY) -O binary $< $@

$(HEX): $(BIN) ../../tests/directed/bin2hex.py
	python3 ../../tests/directed/bin2hex.py $< $@

build:
	mkdir -p $@

run-iss: $(ELF) ../../sim/axsim/axsim
	../../sim/axsim/axsim --bin $<

run-qemu: $(ELF)
	timeout --foreground 10s $(QEMU) -machine virt -bios none \
	  -cpu rv32,pmp=false -nographic -kernel $<

run-rtl: $(HEX)
	$(MAKE) -C ../../sim/soc run RAM_INIT_FILE=$(abspath $<) RESET_PC=0x80000000 \
	  UART_INPUT_FILE=$(UART_INPUT_FILE)

check-boot: images
	$(MAKE) -C ../../sim/axsim axsim
	python3 check_boot.py

clean:
	rm -rf build

.PHONY: all images run-iss run-qemu run-rtl check-boot clean
