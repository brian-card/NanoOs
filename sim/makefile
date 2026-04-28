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
CFLAGS := $(CPU) -std=gnu17 -g -rdynamic

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

OS_OBJECTS := \
    $(OBJ_DIR)/Console.o \
    $(OBJ_DIR)/Coroutines.o \
    $(OBJ_DIR)/ExFatFilesystem.o \
    $(OBJ_DIR)/Filesystem.o \
    $(OBJ_DIR)/HalCommon.o \
    $(OBJ_DIR)/HalPosix.o \
    $(OBJ_DIR)/HalPosixImpl.o \
    $(OBJ_DIR)/Link.o \
    $(OBJ_DIR)/MemoryManager.o \
    $(OBJ_DIR)/Messages.o \
    $(OBJ_DIR)/NanoOs.o \
    $(OBJ_DIR)/NanoOsHardware.o \
    $(OBJ_DIR)/Overlay.o \
    $(OBJ_DIR)/Tasks.o \
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
all: $(BINARY)

# Build simulator binary
$(BINARY): $(COMPONENTS) $(SIM_OBJECTS)
	$(MKDIR) "$(BIN_DIR)"
	$(COMPILE) $(WARNINGS) $(CFLAGS) $(INCLUDES) \
		$(SIM_OBJECTS) $(OS_OBJECTS) -o $@

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
	$(RM) $(SIM_OBJECTS) $(BINARY) $(TARGET).dis
	for component in $(COMPONENTS); do $(MAKE) -C $${component} clean; done

# Show help
help:
	@echo "NanoOS Overlay Build System"
	@echo "Usage:"
	@echo "  make          - Build the simulator"
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

