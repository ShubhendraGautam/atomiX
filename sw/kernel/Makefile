RISCV_PREFIX ?= riscv64-unknown-elf-
RISCV_ARCH   ?= rv32im
PYTHON ?= python3
KERNEL_CONFIG ?= ../../configs/kernel-default.json
# A profile may be outside this worktree; use its absolute location in the
# generated filename so equal basenames cannot collide.
KERNEL_CONFIG_KEY := $(subst /,_,$(basename $(abspath $(KERNEL_CONFIG))))
KERNEL_COMPONENT_MK := build/component$(KERNEL_CONFIG_KEY).mk
KERNEL_COMPONENT_MANIFESTS := $(wildcard ../../components/*/*/component.json)

$(KERNEL_COMPONENT_MK): $(KERNEL_CONFIG) ../../tools/configure.py $(KERNEL_COMPONENT_MANIFESTS)
	mkdir -p $(@D)
	$(PYTHON) ../../tools/configure.py resolve --config "$(KERNEL_CONFIG)" --output $@

-include $(KERNEL_COMPONENT_MK)
$(KERNEL_COMPONENT_MK): $(COMPONENT_SELECTED_MANIFESTS)

.DEFAULT_GOAL := all

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
FS_BLOCK ?= 0

CPPFLAGS := -Iinclude -DAXOS_SD=$(STORAGE) -DAXFS_BLOCK=$(FS_BLOCK)
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
SOURCES := boot.S trap.S user.S kernel.c \
           $(COMPONENT_ALLOCATOR_SOURCES) $(COMPONENT_SHELL_SOURCES) \
           $(COMPONENT_FILESYSTEM_SOURCES) $(COMPONENT_BLOCK_SOURCES) \
           $(COMPONENT_SCHEDULER_SOURCES) $(COMPONENT_VM_SOURCES)

all: $(ELF)
images: $(BIN) $(HEX)

$(ELF): $(SOURCES) $(KERNEL_COMPONENT_MK) link.ld include/platform.h include/page.h include/task.h include/scheduler.h include/vm.h include/fs.h include/sd.h | build
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

check-storage-write: images
	$(MAKE) -B images STORAGE=1
	python3 make_fs_image.py build/axfs.img
	SD_IMAGE=$(abspath build/axfs.img) python3 check_boot.py --storage-write

boot-disk: images
	$(MAKE) -B images STORAGE=1 FS_BLOCK=64
	$(MAKE) -C ../bootrom images
	python3 make_boot_disk.py build/axos_boot.bin build/axos_boot.img

check-sdboot: boot-disk
	SD_IMAGE=$(abspath build/axos_boot.img) BOOT_ROM=$(abspath ../bootrom/build/bootrom.hex) python3 check_boot.py --sd-boot

kernel-config: $(KERNEL_COMPONENT_MK)
	@test -n "$(COMPONENT_SCHEDULER_ID)" || { echo "$(KERNEL_CONFIG): select a scheduler component"; exit 2; }
	@test -n "$(COMPONENT_VM_ID)" || { echo "$(KERNEL_CONFIG): select a vm component"; exit 2; }
	@test -n "$(COMPONENT_ALLOCATOR_ID)" || { echo "$(KERNEL_CONFIG): select an allocator component"; exit 2; }
	@test -n "$(COMPONENT_SHELL_ID)" || { echo "$(KERNEL_CONFIG): select a shell component"; exit 2; }
	@test -n "$(COMPONENT_FILESYSTEM_ID)" || { echo "$(KERNEL_CONFIG): select a filesystem component"; exit 2; }
	@test -n "$(COMPONENT_BLOCK_ID)" || { echo "$(KERNEL_CONFIG): select a block component"; exit 2; }
	@echo "[kernel] scheduler: $(COMPONENT_SCHEDULER_ID)"
	@echo "[kernel] vm:        $(COMPONENT_VM_ID)"
	@echo "[kernel] allocator: $(COMPONENT_ALLOCATOR_ID)"
	@echo "[kernel] shell:     $(COMPONENT_SHELL_ID)"
	@echo "[kernel] filesystem: $(COMPONENT_FILESYSTEM_ID)"
	@echo "[kernel] block:     $(COMPONENT_BLOCK_ID)"

kernel-config-check:
	$(PYTHON) ../../tools/configure.py resolve --config "$(KERNEL_CONFIG)"

kernel-config-check-all:
	@for kernel_config in ../../configs/kernel-*.json; do \
	  $(PYTHON) ../../tools/configure.py resolve --config "$$kernel_config" >/dev/null || exit $$?; \
	  echo "kernel configuration: $$kernel_config: PASS"; \
	done

# Both policies must preserve the same shell/fork semantics. The cooperative
# profile is an actual alternate scheduler, not merely a manifest parse test.
kernel-component-test: kernel-config-check-all
	$(MAKE) -B check-boot KERNEL_CONFIG=$(abspath ../../configs/kernel-default.json)
	$(MAKE) -B check-boot KERNEL_CONFIG=$(abspath ../../configs/kernel-cooperative.json)

clean:
	rm -rf build

.PHONY: all images run-iss run-qemu run-rtl check-boot check-memory check-storage check-storage-write boot-disk check-sdboot kernel-config kernel-config-check kernel-config-check-all kernel-component-test clean
