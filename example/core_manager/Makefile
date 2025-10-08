# Copyright 2021, Breakaway Consulting Pty. Ltd.
# SPDX-License-Identifier: BSD-2-Clause

# Variables
TOOLCHAIN := aarch64-none-elf
CPU ?= cortex-a53   # default if not overridden
CC := $(TOOLCHAIN)-gcc
LD := $(TOOLCHAIN)-ld
AS := $(TOOLCHAIN)-as
MICROKIT_TOOL ?= $(MICROKIT_SDK)/bin/microkit

BOARD_DIR := $(MICROKIT_SDK)/board/$(MICROKIT_BOARD)/$(MICROKIT_CONFIG)
CORE_DIR := core
BOOTSTRAP_DIR := bootstrap
IMAGES := core_manager.elf core_manager_api.elf core_worker.elf
CFLAGS := -mcpu=$(CPU) -mstrict-align -nostdlib -ffreestanding -g -O3 -Wall -Wno-unused-function -Werror \
          -I. -I$(BOARD_DIR)/include -I$(CORE_DIR) -DNUM_CPUS=$(NUM_CPUS)
LDFLAGS := -L$(BOARD_DIR)/lib -T $(BOOTSTRAP_DIR)/bootstrap.ld
LIBS := -lmicrokit -Tmicrokit.ld

IMAGE_FILE = $(BUILD_DIR)/loader.img
REPORT_FILE = $(BUILD_DIR)/report.txt
QEMU := qemu-system-aarch64

BOOTSTRAP_OBJS := bootstrap_c.o bootstrap_s.o

# === CONFIGURABLE INPUTS ===
ifeq ($(strip $(CORE_SYSTEM)),)
$(error CORE_SYSTEM must be specified, e.g. 'make CORE_SYSTEM=my_config.system')
endif

.PHONY: all clean qemu qemu-debug

# Ensure necessary environment variables are set
ifeq ($(strip $(BUILD_DIR)),)
$(error BUILD_DIR must be specified)
endif

ifeq ($(strip $(MICROKIT_SDK)),)
$(error MICROKIT_SDK must be specified)
endif

ifeq ($(strip $(MICROKIT_BOARD)),)
$(error MICROKIT_BOARD must be specified)
endif

ifeq ($(strip $(MICROKIT_CONFIG)),)
$(error MICROKIT_CONFIG must be specified)
endif

ifeq ($(strip $(NUM_CPUS)),)
$(error NUM_CPUS must be specified)
endif

# Default target
all: $(IMAGE_FILE)

# Compile C source files (generic rule)
$(BUILD_DIR)/%.o: %.c Makefile
	$(CC) -c $(CFLAGS) $< -o $@

# Compile bootstrap components
$(BUILD_DIR)/bootstrap_c.o: $(BOOTSTRAP_DIR)/bootstrap.c Makefile
	$(CC) -c $(CFLAGS) -ffunction-sections -fdata-sections $< -o $@

$(BUILD_DIR)/bootstrap_s.o: $(BOOTSTRAP_DIR)/bootstrap.S Makefile
	$(CC) -c $(CFLAGS) $< -o $@

# Compile core components
$(BUILD_DIR)/core_manager_api.o: $(CORE_DIR)/core_manager_api.c Makefile
	$(CC) -c $(CFLAGS) $< -o $@

$(BUILD_DIR)/core_worker.o: $(CORE_DIR)/core_worker.c Makefile
	$(CC) -c $(CFLAGS) $< -o $@

$(BUILD_DIR)/core_manager.o: $(CORE_DIR)/core_manager.c Makefile
	$(CC) -c $(CFLAGS) $< -o $@

# Link core_manager_api.elf including bootstrap object files
$(BUILD_DIR)/core_manager_api.elf: $(addprefix $(BUILD_DIR)/, core_manager_api.o $(BOOTSTRAP_OBJS))
	$(LD) $(LDFLAGS) $^ $(LIBS) -o $@

# Link core_worker.elf
$(BUILD_DIR)/core_worker.elf: $(addprefix $(BUILD_DIR)/, core_worker.o)
	$(LD) $(LDFLAGS) $^ $(LIBS) -o $@

# Link core_manager.elf
$(BUILD_DIR)/core_manager.elf: $(addprefix $(BUILD_DIR)/, core_manager.o)
	$(LD) $(LDFLAGS) $^ $(LIBS) -o $@

# Generate image and report
$(IMAGE_FILE) $(REPORT_FILE): $(addprefix $(BUILD_DIR)/, $(IMAGES)) $(CORE_SYSTEM)
	$(MICROKIT_TOOL) $(CORE_SYSTEM) --search-path $(BUILD_DIR) \
		--board $(MICROKIT_BOARD) --config $(MICROKIT_CONFIG) \
		-o $(IMAGE_FILE) -r $(REPORT_FILE)

clean:
	rm -f $(BUILD_DIR)/*.o $(BUILD_DIR)/*.elf $(IMAGE_FILE) $(REPORT_FILE)

qemu: $(IMAGE_FILE)
	@if ! command -v $(QEMU) > /dev/null 2>&1; then echo "Could not find dependency: qemu-system-aarch64"; exit 1; fi
	$(QEMU) -machine virt,virtualization=on,highmem=off,secure=off,gic-version=2 \
		-cpu $(CPU) \
		-serial mon:stdio \
		-smp 4 \
		-device loader,file=$(IMAGE_FILE),addr=0x70000000,cpu-num=0 \
		-m size=2G \
		-nographic

qemu-debug: $(IMAGE_FILE)
	@if ! command -v $(QEMU) > /dev/null 2>&1; then echo "Could not find dependency: qemu-system-aarch64"; exit 1; fi
	$(QEMU) -machine virt,virtualization=on,highmem=off,secure=off,gic-version=2 \
		-cpu $(CPU) \
		-serial mon:stdio \
		-smp 4 \
		-device loader,file=$(IMAGE_FILE),addr=0x70000000,cpu-num=0 \
		-m size=2G \
		-nographic \
		-s -S \
		-d guest_errors \
		-D qemu_debug.log
