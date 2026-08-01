# Builds a single overlay slot as the union of common/$(SLOT) (driver-agnostic
# wrapper code) and $(FILESYSTEM)/$(SLOT) (the selected driver's
# implementation for this slot, if any).  All paths here are relative to
# usr/src/filesystems/overlay/, which is where this file is always invoked
# from (see makefile).

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

OBJ_DIR := ../../../obj/overlay/$(SLOT)
BIN_DIR := ../../../filesystem/overlay

COMMON_DIR := common/$(SLOT)
DRIVER_DIR := $(FILESYSTEM)/$(SLOT)
VPATH := $(COMMON_DIR):$(DRIVER_DIR)

# Full paths (needed by mkOverlayMap.sh) and their basenames (needed to
# compute object file paths via VPATH).
SOURCE_PATHS := $(wildcard $(COMMON_DIR)/*.c) $(wildcard $(DRIVER_DIR)/*.c)
SOURCES := $(notdir $(SOURCE_PATHS))

ELF := $(OBJ_DIR)/overlay.elf
BINARY := $(BIN_DIR)/$(SLOT).overlay

OBJECTS := \
    $(OBJ_DIR)/OverlayMap.o \
    $(addprefix $(OBJ_DIR)/,$(SOURCES:.c=.o)) \

INCLUDES += \
    -I../../../../src/kernel \
    -I../../../../src/user \
    -I../../../include \
    -I../include \
    -Icommon/include \
    -I../drivers/$(FILESYSTEM)/include \

.PHONY: all clean

all: $(BINARY)

$(BINARY): $(ELF)
	@echo "Creating binary: $@"
	$(MKDIR) "$(BIN_DIR)"
	$(OBJCOPY) -O binary $< $@
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
	$(RM) $(OBJECTS) $(ELF) $(BINARY)
