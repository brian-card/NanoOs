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
    -I../user \
    -I../include \

WARNINGS = \
    -Wall \
    -Wextra \
    -pedantic \
    -Werror \

SOURCES = \

OBJECTS = \
    $(OBJ_DIR)/Commands.o \
    $(OBJ_DIR)/Console.o \
    $(OBJ_DIR)/Coroutines.o \
    $(OBJ_DIR)/ExFatFilesystem.o \
    $(OBJ_DIR)/ExFatTask.o \
    $(OBJ_DIR)/Filesystem.o \
    $(OBJ_DIR)/Link.o \
    $(OBJ_DIR)/MemoryManager.o \
    $(OBJ_DIR)/Messages.o \
    $(OBJ_DIR)/NanoOs.o \
    $(OBJ_DIR)/NanoOsOverlay.o \
    $(OBJ_DIR)/Tasks.o \
    $(OBJ_DIR)/Scheduler.o \
    $(OBJ_DIR)/SdCard.o \

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

