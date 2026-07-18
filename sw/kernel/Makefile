RISCV_PREFIX ?= riscv64-unknown-elf-
RISCV_ARCH   ?= rv32im
CC      := $(RISCV_PREFIX)gcc
OBJCOPY := $(RISCV_PREFIX)objcopy
QEMU    ?= qemu-system-riscv32
export QEMU
RAM_BYTES ?= 131072
EXTERNAL_MEMORY ?= 0
CACHES ?= 0
MAX_CYCLES ?= 100000
STORAGE ?= 0
SD_IMAGE ?=

CPPFLAGS := -Iinclude -DAXOS_SD=$(STORAGE)
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
SOURCES := boot.S trap.S user.S kernel.c page.c shell.c fs.c sd.c

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
	  UART_INPUT_FILE=$(UART_INPUT_FILE) RAM_BYTES=$(RAM_BYTES) \
	  EXTERNAL_MEMORY=$(EXTERNAL_MEMORY) CACHES=$(CACHES) MAX_CYCLES=$(MAX_CYCLES) SD_IMAGE=$(SD_IMAGE)

check-boot: images
	$(MAKE) -B images STORAGE=0
	$(MAKE) -C ../../sim/axsim axsim
	python3 check_boot.py

check-memory: images
	$(MAKE) -B images STORAGE=0
	python3 check_boot.py --external-memory

check-storage: images
	$(MAKE) -B images STORAGE=1
	python3 make_fs_image.py build/axfs.img
	SD_IMAGE=$(abspath build/axfs.img) python3 check_boot.py --external-memory

clean:
	rm -rf build

.PHONY: all images run-iss run-qemu run-rtl check-boot check-memory check-storage clean
