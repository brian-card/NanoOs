# All paths are relative to the command overlay directories

include ../../include.mk

ifneq ($(LINKER_SCRIPT),)
    override LINKER_SCRIPT := -T ../../../$(LINKER_SCRIPT)
endif

# Compiler flags
CFLAGS += -Os -nostdlib -ffreestanding
CFLAGS += -fno-pic -fno-pie -static
CFLAGS += -ffunction-sections -fdata-sections -fcf-protection=none
CFLAGS += -fno-jump-tables
CFLAGS += -fno-stack-protector

# Linker flags
LDFLAGS += $(LINKER_SCRIPT) -Wl,--gc-sections -static -no-pie
LDFLAGS += -Wl,--build-id=none -nostartfiles

OBJ_DIR = ../../../../obj
BIN_DIR = ../../../../lib

# Source and target files
ELF = $(OBJ_DIR)/$(TARGET)/$(OVERLAY)/overlay.elf
BINARY = $(BIN_DIR)/$(TARGET)/$(OVERLAY).overlay

SOURCES += \
    $(shell ls | grep -Ev 'makefile|.*\.mk|OverlayMap.c') \

OBJECTS := \
    $(OBJ_DIR)/$(TARGET)/$(OVERLAY)/OverlayMap.o \
    $(addprefix $(OBJ_DIR)/$(TARGET)/$(OVERLAY)/, $(SOURCES:.c=.o)) \

OBJECTS := $(subst $(OBJ_DIR)/$(TARGET)/$(OVERLAY)/../../../start.o,\
    $(OBJ_DIR)/start.o,\
    $(OBJECTS))

INCLUDES += \
    -I../../../../../src/kernel \
    -I../../../../../src/user \
    -I../../include \

# Default target
all: $(BINARY)

# Build overlay binary
$(BINARY): $(ELF)
	@echo "Creating binary: $@"
	$(MKDIR) "$(BIN_DIR)/$(TARGET)"
	$(OBJCOPY) -O binary $< $@
	@echo "Binary size:"
	@ls -la $@

# Link ELF file
$(ELF): $(OBJECTS)
	@echo "Linking: $@"
	$(MKDIR) "$(OBJ_DIR)"
	$(COMPILE) $(LDFLAGS) $(OBJECTS) $(LINKS) -o $@
	$(SIZE) $@

# Compile object files
$(OBJ_DIR)/start.o: ../../../start.c
	@echo "Compiling: $<"
	$(MKDIR) "$(OBJ_DIR)"
	$(COMPILE) $(WARNINGS) $(CFLAGS) $(INCLUDES) -c $< -o $@

$(OBJ_DIR)/$(TARGET)/$(OVERLAY)/%.o: %.c
	@echo "Compiling: $<"
	$(MKDIR) "$(OBJ_DIR)/$(TARGET)/$(OVERLAY)"
	$(COMPILE) $(WARNINGS) $(CFLAGS) $(INCLUDES) -c $< -o $@

OverlayMap.c: $(SOURCES)
	@echo "Creating OverlayMap.c"
	../../../util/mkOverlayMap.sh OverlayMap.c $(SOURCES)

# Generate disassembly for debugging
disasm: $(ELF)
	$(OBJDUMP) -d -S $< > $(TARGET).dis
	@echo "Disassembly written to $(TARGET).dis"

# Show section information
sections: $(ELF)
	$(OBJDUMP) -h $<

# Show symbol table
symbols: $(ELF)
	$(OBJDUMP) -t $<

# Clean build artifacts
clean:
	$(RM) $(OBJECTS) $(ELF) $(BINARY) $(TARGET).dis OverlayMap.c

# Show help
help:
	@echo "NanoOS Overlay Build System"
	@echo "Usage:"
	@echo "  make          - Build $(TARGET)"
	@echo "  make disasm   - Generate disassembly listing"
	@echo "  make sections - Show ELF section information"  
	@echo "  make symbols  - Show symbol table"
	@echo "  make clean    - Remove build artifacts"
	@echo "  make help     - Show this help"
	@echo ""
	@echo "Include paths:"
	@$(COMPILE) $(CFLAGS) $(INCLUDES) -E -Wp,-v -x c /dev/null 2>&1 | grep "^ "

# Phony targets
.PHONY: all clean disasm sections symbols help OverlayMap.c

