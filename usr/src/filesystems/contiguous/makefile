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

OBJ_DIR := ../../../obj/contiguous
BIN_DIR := ../../../filesystem/contiguous

VPATH := common:fat32

# Full paths (needed by mkOverlayMap.sh) and their basenames (needed to
# compute object file paths via VPATH).
SOURCE_PATHS := $(wildcard common/*.c) $(wildcard fat32/*.c)
SOURCES := $(notdir $(SOURCE_PATHS))

ELF := $(OBJ_DIR)/overlay.elf
BINARY := $(BIN_DIR)/0.overlay

OBJECTS := \
    $(OBJ_DIR)/OverlayMap.o \
    $(addprefix $(OBJ_DIR)/,$(SOURCES:.c=.o)) \

INCLUDES += \
    -I../../../../src/kernel \
    -I../../../../src/user \
    -I../../../include \
    -I../include \
    -I../drivers/$(FILESYSTEM)/include \

# Maximum size, in bytes, of the contiguous overlay binary.  It is placed at
# a fixed block offset (see buildsim/builddev) with no per-slot budget to
# grow into, so a build that produces a binary larger than this must fail
# rather than silently corrupt whatever follows it on disk.
MAX_CONTIGUOUS_SIZE := 32768

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

$(OBJ_DIR)/%.o: %.c
	@echo "Compiling: $<"
	$(MKDIR) "$(OBJ_DIR)"
	$(COMPILE) $(WARNINGS) $(CFLAGS) $(INCLUDES) -c $< -o $@

# OverlayMap.c is generated directly into OBJ_DIR, so it isn't reachable via
# VPATH -- give it an explicit compile rule rather than relying on the
# generic pattern rule above.
$(OBJ_DIR)/OverlayMap.o: $(OBJ_DIR)/OverlayMap.c
	@echo "Compiling: $<"
	$(COMPILE) $(WARNINGS) $(CFLAGS) $(INCLUDES) -c $< -o $@

$(OBJ_DIR)/OverlayMap.c: $(SOURCE_PATHS)
	@echo "Creating OverlayMap.c"
	$(MKDIR) "$(OBJ_DIR)"
	../../util/mkOverlayMap.sh $(OBJ_DIR)/OverlayMap.c $(SOURCE_PATHS)

clean:
	$(RM) $(OBJ_DIR) $(BIN_DIR)
