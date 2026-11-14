#CC       := g++ -std=gnu++17
CC       := gcc -std=gnu17
MAKEFILE := makefile
OBJ_DIR := ../../obj
EXE_DIR := ../../exe
OPENSSL_DIR := /usr/lib/openssl/linux64
EXTRA_FLAGS := -rdynamic
EXTRA_DEFINES := \

EXTRA_COMMON_FILES := \

MKDIR        := mkdir -p
COPY         := cp
REMOVE       := rm -f

FLAGS := -g $(EXTRA_FLAGS) $(COMMAND_LINE_FLAGS)

COMMON_FILES := \
    ../../../../src/os/Link.c

INCLUDES := \
    -I../../../../src/os \

LINKS := \

WARNINGS := \
    -Wall \
    -Wextra \
    -Werror \

all: $(EXE_DIR)/makeLink $(EXE_DIR)/getTarget $(EXE_DIR)/openTarget

$(EXE_DIR)/%: $(COMMON_FILES) %.c
	@$(MKDIR) $(EXE_DIR)
	$(CC) $(FLAGS) $(INCLUDES) $(DEFINES) $(WARNINGS) $^ $(LINKS) -o $@
