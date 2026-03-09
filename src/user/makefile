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
CFLAGS = $(CPU) -std=gnu17 -g -rdynamic

LINKS = \

INCLUDES = \
    -I../kernel \

WARNINGS = \
    -Wall \
    -Wextra \
    -pedantic \
    -Werror \
    -Wno-nonnull-compare \

SOURCES = \

OBJECTS = \
    $(OBJ_DIR)/NanoOsApi.o \
    $(OBJ_DIR)/NanoOsErrno.o \
    $(OBJ_DIR)/NanoOsHardware.o \
    $(OBJ_DIR)/NanoOsLibC.o \
    $(OBJ_DIR)/NanoOsPwd.o \
    $(OBJ_DIR)/NanoOsSched.o \
    $(OBJ_DIR)/NanoOsStdio.o \
    $(OBJ_DIR)/NanoOsSys.o \
    $(OBJ_DIR)/NanoOsTermios.o \
    $(OBJ_DIR)/NanoOsUnistd.o \

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

