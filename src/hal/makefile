# This makefile exist for the simulator, so make the paths relative to that.
OBJ_DIR = ../../sim/obj
BIN_DIR = ../../sim/bin

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

# Linker flags
LDFLAGS = 

# Compiler flags
#
# -fno-jump-tables: without it, gcc lowers switch statements with dense case
# ranges to a jump table in .rodata. sim/makefile's stripped binary removes
# .rodata to mirror the AgonLight2/SAMD21 shipped-binary build, so a jump
# table there would crash on first use just like it would on real hardware.
# Matches makefiles/AgonLight2Makefile.
CFLAGS = $(CPU) -std=gnu17 -g -fno-jump-tables

LINKS = \

INCLUDES = \
    -I../../sim \

WARNINGS = \
    -Wall \
    -Wextra \
    -pedantic \
    -Werror \

SOURCES = \

OBJECTS = \
    $(OBJ_DIR)/HalCommon.o \
    $(OBJ_DIR)/HalPosix.o \
    $(OBJ_DIR)/HalPosixImpl.o \

# Default target
all: $(OBJECTS)

# Compile object files
$(OBJ_DIR)/%.o: %.c
	$(MKDIR) "$(OBJ_DIR)"
	$(COMPILE) $(WARNINGS) $(CFLAGS) $(INCLUDES) -c $< -o $@

# Clean build artifacts
clean:
	$(RM) $(OBJECTS)

# Phony targets
.PHONY: all clean

