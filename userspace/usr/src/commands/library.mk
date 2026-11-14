# All paths are relative to the command overlay directories

include ../../include.mk

ifneq ($(LINKER_SCRIPT),)
    override LINKER_SCRIPT := -T ../../../$(LINKER_SCRIPT)
endif

OBJ_DIR = ../../../../obj
BIN_DIR = ../../../../bin

# Source and target files
ELF = $(OBJ_DIR)/$(TARGET)/$(LIBRARY).o

SOURCES += \
    $(shell ls | grep -Ev 'makefile|.*\.mk|OverlayMap.c') \

OBJECTS := \
    $(addprefix $(OBJ_DIR)/$(TARGET)/$(LIBRARY)/, $(SOURCES:.c=.o)) \

OBJECTS := $(subst $(OBJ_DIR)/$(TARGET)/$(LIBRARY)/../../../start.o,\
    $(OBJ_DIR)/start.o,\
    $(OBJECTS))

INCLUDES := \

# Default target
all: $(ELF)

# Create the main library ELF object
$(ELF): $(OBJECTS)
	@echo "Linking: $@"
	$(MKDIR) "$(OBJ_DIR)/$(TARGET)"
	$(LINK) -r $(LDFLAGS) $(OBJECTS) $(LINKS) -o $@
	$(SIZE) $@

# Compile object files
$(OBJ_DIR)/$(TARGET)/$(LIBRARY)/%.o: %.c
	@echo "Compiling: $<"
	$(MKDIR) "$(OBJ_DIR)/$(TARGET)/$(LIBRARY)"
	$(COMPILE) $(WARNINGS) $(CFLAGS) $(INCLUDES) -c $< -o $@

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
	$(RM) $(OBJECTS) $(ELF) $(TARGET).dis

# Show help
help:
	@echo "NanoOS Application Build System"
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
.PHONY: all clean disasm sections symbols help

