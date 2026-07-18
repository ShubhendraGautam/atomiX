RISCV_PREFIX ?= riscv64-unknown-elf-
CC := $(RISCV_PREFIX)gcc
OBJCOPY := $(RISCV_PREFIX)objcopy
CFLAGS := -march=rv32im -mabi=ilp32 -mcmodel=medany -mno-relax -ffreestanding \
  -fno-builtin -fno-pic -fno-stack-protector -Wall -Wextra -Werror -Os
ELF := build/bootrom.elf

all: $(ELF)
images: $(ELF:.elf=.hex)
$(ELF): boot.S boot.c link.ld | build
	$(CC) $(CFLAGS) -nostdlib -Wl,-T,link.ld -Wl,--gc-sections -o $@ boot.S boot.c
build:
	mkdir -p $@
build/bootrom.bin: $(ELF)
	$(OBJCOPY) -O binary $< $@
build/bootrom.hex: build/bootrom.bin ../../tests/directed/bin2hex.py
	python3 ../../tests/directed/bin2hex.py $< $@
clean:
	rm -rf build
.PHONY: all images clean
