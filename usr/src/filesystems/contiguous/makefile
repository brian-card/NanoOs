# Builds the single contiguous-filesystem overlay: the union of common/
# (driver-agnostic dispatch loop and command handlers, always present) and
# fat32/ (the selected driver's implementation, symlinked in from
# ../drivers/$(FILESYSTEM)).  All paths here are relative to
# usr/src/filesystems/contiguous/.

include include.mk

ifneq ($(LINKER_SCRIPT),)
    override LINKER_SCRIPT := -T ../../$(LINKER_SCRIPT)
endif

# Compiler flags
CFLAGS += -Os -nostdlib -ffreestanding -g
CFLAGS += -fno-pic -fno-pie -static
CFLAGS += -ffunction-sections -fdata-sections -fcf-protection=none
CFLAGS += -fno-jump-tables
CFLAGS += -fno-stack-protector

# Linker flags
ifdef USE_LD_DIRECTLY
LDFLAGS += $(LINKER_SCRIPT) --gc-sections
LDFLAGS += --build-id=none
else
LDFLAGS += $(LINKER_SCRIPT) -Wl,--gc-sections -static -no-pie
LDFLAGS += -Wl,--build-id=none -nostartfiles
endif

OBJ_DIR := ../../../obj/contiguous-filesystem
BIN_DIR := ../../../filesystem/contiguous-filesystem

# Full paths, needed by mkOverlayMap.sh.
HANDLER_PATHS := $(wildcard common/*.c)
DRIVER_PATHS := $(wildcard fat32/*.c)
SOURCE_PATHS := $(HANDLER_PATHS) $(DRIVER_PATHS)

# The command handlers and the driver sources are each compiled as a single
# translation unit, as the kernel-linked build does in
# src/kernel/Fat32Filesystem.c, so that each static inline helper they share
# is emitted once instead of once per source file.
HANDLER_UNIT := $(OBJ_DIR)/Handlers.c
DRIVER_UNIT := $(OBJ_DIR)/Driver.c

ELF := $(OBJ_DIR)/overlay.elf
BINARY := $(BIN_DIR)/0.overlay

OBJECTS := \
    $(OBJ_DIR)/OverlayMap.o \
    $(OBJ_DIR)/Handlers.o \
    $(OBJ_DIR)/Driver.o \

INCLUDES += \
    -I../../../../src/kernel \
    -I../../../../src/user \
    -I../../../include \
    -I../include \
    -I../drivers/$(FILESYSTEM) \

# Maximum size, in bytes, of the contiguous overlay binary.  It is placed at
# a fixed block offset (see buildsim/builddev) with no per-slot budget to
# grow into, so a build that produces a binary larger than this must fail
# rather than silently corrupt whatever follows it on disk.  Platform-specific:
# must match the target's actual contiguous-filesystem RAM window (e.g.
# FILESYSTEM_DRIVER_SIZE in src/hal/HalAgonLight2.c for AgonLight2), so
# buildsim/builddev pass it in per platform; this default covers callers that
# don't.
MAX_CONTIGUOUS_SIZE ?= 32768

.PHONY: all overlays clean

all: $(BINARY)

overlays: $(BINARY)

$(BINARY): $(ELF)
	@echo "Creating binary: $@"
	$(MKDIR) "$(BIN_DIR)"
	$(OBJCOPY) -O binary $< $@
	@size=$$(stat -c%s "$@" 2>/dev/null || stat -f%z "$@"); \
	if [ "$${size}" -gt $(MAX_CONTIGUOUS_SIZE) ]; then \
		echo "ERROR: $@ is $${size} bytes, exceeds the $(MAX_CONTIGUOUS_SIZE) byte limit for the contiguous filesystem overlay." >&2; \
		exit 1; \
	fi
	@echo "Binary size:"
	@ls -la $@

$(ELF): $(OBJECTS)
	@echo "Linking: $@"
	$(MKDIR) "$(OBJ_DIR)"
	$(LINKER) $(LDFLAGS) $(OBJECTS) $(LINKS) -o $@
	$(SIZE) $@

$(OBJ_DIR)/OverlayMap.o: $(OBJ_DIR)/OverlayMap.c
	@echo "Compiling: $<"
	$(COMPILE) $(WARNINGS) $(CFLAGS) $(INCLUDES) -c $< -o $@

$(OBJ_DIR)/Driver.o: $(DRIVER_UNIT)
	@echo "Compiling: $<"
	$(COMPILE) $(WARNINGS) $(CFLAGS) $(INCLUDES) -c $< -o $@

$(OBJ_DIR)/Handlers.o: $(HANDLER_UNIT)
	@echo "Compiling: $<"
	$(COMPILE) $(WARNINGS) $(CFLAGS) $(INCLUDES) -c $< -o $@

$(HANDLER_UNIT): $(HANDLER_PATHS)
	@echo "Creating $@"
	$(MKDIR) "$(OBJ_DIR)"
	printf '#include "%s"\n' $(abspath $(HANDLER_PATHS)) > $@

$(DRIVER_UNIT): $(DRIVER_PATHS)
	@echo "Creating $@"
	$(MKDIR) "$(OBJ_DIR)"
	printf '#include "%s"\n' $(abspath $(DRIVER_PATHS)) > $@

$(OBJ_DIR)/OverlayMap.c: $(SOURCE_PATHS)
	@echo "Creating OverlayMap.c"
	$(MKDIR) "$(OBJ_DIR)"
	../../util/mkOverlayMap.sh $(OBJ_DIR)/OverlayMap.c $(SOURCE_PATHS)

clean:
	$(RM) $(OBJ_DIR) $(BIN_DIR)
