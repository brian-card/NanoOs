# CLI tools
MKDIR := mkdir -p
RM    := rm -rf

# Build tools
ifeq ($(strip $(COMPILE)),)
    override COMPILE       := gcc
endif
ifeq ($(strip $(LINK)),)
    override LINK          := ld
endif
ifeq ($(strip $(OBJCOPY)),)
    override OBJCOPY       := objcopy
endif
ifeq ($(strip $(OBJDUMP)),)
    override OBJDUMP       := objdump
endif
ifeq ($(strip $(SIZE)),)
    override SIZE          := size
endif

# Compiler flags
CFLAGS := -std=gnu17
ifeq ($(COMPILE),arm-none-eabi-gcc)
    CFLAGS += -mcpu=cortex-m0
endif

# Linker flags
ifeq ($(COMPILE),arm-none-eabi-gcc)
    LDFLAGS := -mcpu=cortex-m0
endif

LINKS = \

WARNINGS = \
    -Wall \
    -Wextra \
    -pedantic \
    -Werror \

