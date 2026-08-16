OBJ_DIR    := obj
BIN_DIR    := bin

# CLI tools
MKDIR := mkdir -p
RM    := rm -rf

# Build tools
ifeq ($(strip $(COMPILE)),)
    override COMPILE := gcc
endif
ifeq ($(strip $(LINK)),)
    override LINK    := ld
endif
ifeq ($(strip $(OBJCOPY)),)
    override OBJCOPY := objcopy
endif
ifeq ($(strip $(OBJDUMP)),)
    override OBJDUMP := objdump
endif
ifeq ($(strip $(SIZE)),)
    override SIZE    := size
endif

ifeq ($(COMPILE),arm-none-eabi-gcc)
    CPU := -mcpu=cortex-m0
endif

# Compiler flags
#
# -fno-jump-tables: without it, gcc lowers switch statements with dense case
# ranges to a jump table in .rodata (see e.g. msg_element in Messages.c).
# $(STRIPPED_BINARY) below removes .rodata to mirror the AgonLight2/SAMD21
# shipped-binary build, so a jump table there would crash on first use just
# like it would on real hardware. Matches makefiles/AgonLight2Makefile.
CFLAGS := $(CPU) -std=gnu17 -g -rdynamic -fno-jump-tables

LINKS := \

INCLUDES := \
    -Ihal \
    -Ikernel \
    -Iuser \

WARNINGS := \
    -Wall \
    -Wextra \
    -pedantic \
    -Werror \

COMPONENTS := \
    ../src/hal \
    ../src/kernel \
    ../src/user \

# Source and target files
TARGET := nano-os-sim
BINARY := $(BIN_DIR)/$(TARGET)
# Named NanoOs_rodata.bin (not $(TARGET)_rodata.bin) so it lands on the
# filesystem under the same name the logger process expects on every
# platform (see makefiles/AgonLight2Makefile, ArduinoSamd21Makefile).
RODATA_BIN := $(BIN_DIR)/NanoOs_rodata.bin
STRIPPED_BINARY := $(BIN_DIR)/$(TARGET)_stripped

OS_OBJECTS := \
    $(OBJ_DIR)/Commands.o \
    $(OBJ_DIR)/Console.o \
    $(OBJ_DIR)/Coroutines.o \
    $(OBJ_DIR)/CTypeSupport.o \
    $(OBJ_DIR)/Fat32Filesystem.o \
    $(OBJ_DIR)/Filesystem.o \
    $(OBJ_DIR)/Hal.o \
    $(OBJ_DIR)/HalCommon.o \
    $(OBJ_DIR)/HalPosix.o \
    $(OBJ_DIR)/HalPosixImpl.o \
    $(OBJ_DIR)/Link.o \
    $(OBJ_DIR)/Logger.o \
    $(OBJ_DIR)/MemoryManager.o \
    $(OBJ_DIR)/Messages.o \
    $(OBJ_DIR)/NanoOs.o \
    $(OBJ_DIR)/NanoOsHardware.o \
    $(OBJ_DIR)/Overlay.o \
    $(OBJ_DIR)/Processes.o \
    $(OBJ_DIR)/Scheduler.o \
    $(OBJ_DIR)/SdCard.o \
    $(OBJ_DIR)/SdCardSpi.o \
    $(OBJ_DIR)/NanoOsApi.o \
    $(OBJ_DIR)/NanoOsErrno.o \
    $(OBJ_DIR)/NanoOsFcntl.o \
    $(OBJ_DIR)/NanoOsLibC.o \
    $(OBJ_DIR)/NanoOsPwd.o \
    $(OBJ_DIR)/NanoOsSched.o \
    $(OBJ_DIR)/NanoOsSignal.o \
    $(OBJ_DIR)/NanoOsSpawn.o \
    $(OBJ_DIR)/NanoOsStdio.o \
    $(OBJ_DIR)/NanoOsSysUtsname.o \
    $(OBJ_DIR)/NanoOsTermios.o \
    $(OBJ_DIR)/NanoOsUnistd.o \

SIM_OBJECTS := \
    $(OBJ_DIR)/NanoOsSim.o \
    $(OBJ_DIR)/SdCardPosix.o \
    $(OBJ_DIR)/SdCardPosixImpl.o \

# Default target
all: $(BINARY) $(RODATA_BIN) $(STRIPPED_BINARY)

# Build simulator binary
$(BINARY): $(COMPONENTS) $(SIM_OBJECTS)
	$(MKDIR) "$(BIN_DIR)"
	$(COMPILE) $(WARNINGS) $(CFLAGS) $(INCLUDES) \
		$(SIM_OBJECTS) $(OS_OBJECTS) -o $@

# Flat binary containing only the .rodata section, for inspection alongside
# the final program binary. The POSIX/sim target never strips .rodata from
# $(BINARY) itself (it's a native ELF, not a flashed image), so this is a
# separate extract rather than a companion to a stripped build.
$(RODATA_BIN): $(BINARY)
	$(OBJCOPY) -O binary -j .rodata $< $@

# Copy of $(BINARY) with .rodata removed, mirroring the objcopy -R .rodata
# step the AgonLight2/SAMD21 builds run on their shipped binaries (see
# makefiles/AgonLight2Makefile), so the same missing-.rodata code paths can
# be exercised on the dev machine instead of only on real hardware.
$(STRIPPED_BINARY): $(BINARY)
	$(OBJCOPY) -R .rodata $< $@

../src/%: FORCE
	$(MAKE) -C $@ COMPILE=$(COMPILE) LINK=$(LINK) \
		OBJCOPY=$(OBJCOPY) OBJDUMP=$(OBJDUMP) SIZE=$(SIZE)

# Compile object files
$(OBJ_DIR)/%.o: %.c
	$(MKDIR) "$(OBJ_DIR)"
	$(COMPILE) $(WARNINGS) $(CFLAGS) $(INCLUDES) -c $< -o $@

# Generate disassembly for debugging
disasm: $(BINARY)
	$(OBJDUMP) -d -S $< > $(TARGET).dis
	@echo "Disassembly written to $(TARGET).dis"

# Show section information
sections: $(BINARY)
	$(OBJDUMP) -h $<

# Show symbol table
symbols: $(BINARY)
	$(OBJDUMP) -t $<

# Clean build artifacts
clean:
	$(RM) $(SIM_OBJECTS) $(BINARY) $(RODATA_BIN) $(STRIPPED_BINARY) $(TARGET).dis
	for component in $(COMPONENTS); do $(MAKE) -C $${component} clean; done

# Show help
help:
	@echo "NanoOS Overlay Build System"
	@echo "Usage:"
	@echo "  make          - Build the simulator and its .rodata-only/-stripped artifacts"
	@echo "  make disasm   - Generate disassembly listing"
	@echo "  make sections - Show ELF section information"  
	@echo "  make symbols  - Show symbol table"
	@echo "  make clean    - Remove build artifacts"
	@echo "  make help     - Show this help"
	@echo ""
	@echo "Include paths:"
	@$(COMPILE) $(CFLAGS) $(INCLUDES) -E -Wp,-v -x c /dev/null 2>&1 | grep "^ "

FORCE:

# Phony targets
.PHONY: all clean disasm sections symbols help

