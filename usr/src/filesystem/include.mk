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
ifneq ($(findstring ez80,$(COMPILE)),)
    CFLAGS += --target=ez80-none-elf
    CFLAGS += -mllvm -z80-gas-style
    CFLAGS += -mllvm -z80-print-zero-offset
    CFLAGS += -Wa,-march=ez80+full
endif

INCLUDES += $(EXTRA_INCLUDES)

# Linker to use for the link step (compiler driver by default; bare ld for
# toolchains whose driver does not know how to invoke the target linker).
LINKER := $(COMPILE)
ifneq ($(findstring ez80,$(COMPILE)),)
    LINKER := $(LINK)
    USE_LD_DIRECTLY := 1
endif

# Linker flags
ifeq ($(COMPILE),arm-none-eabi-gcc)
    LDFLAGS := -mcpu=cortex-m0
endif

LINKS = $(EXTRA_LINKS)

WARNINGS = \
    -Wall \
    -Wextra \
    -pedantic \
    -Werror \

