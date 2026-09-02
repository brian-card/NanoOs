# NanoOs kernel test harness.
#
# This build is deliberately self-contained.  It has its own object
# directory (test/obj) and never writes into sim/obj, so running it can
# never leave stale or differently-flagged objects behind for buildsim,
# the AgonLight2 build, or any of the Arduino builds.  Those builds
# discover their sources with `find src ...` / explicit object lists and
# do not look at test/ at all.
#
# Usage:
#   make -C test            # build + run every suite
#   make -C test build      # build only
#   make -C test run        # run the built harness
#   make -C test clean
#   make -C test ASAN=1     # build with ASan/UBSan
#   ./test/bin/nano-os-test [filter]
#
# Per-group include paths and flags mirror what each src/*/makefile and
# sim/makefile pass for their own translation units (the sim/ directory
# holds hal/ kernel/ user/ symlinks into ../src, which is how the kernel
# sources resolve their "kernel/..." / "user/..." includes without putting
# src/kernel on the header search path - that would shadow <sys/types.h>).

# ASan and non-ASan objects are not link-compatible, so they get separate
# trees - switching `make -C test` <-> `make -C test ASAN=1` never needs a
# manual clean.
ifdef ASAN
  VARIANT := -asan
else
  VARIANT :=
endif

OBJ_DIR := obj$(VARIANT)
BIN_DIR := bin

MKDIR := mkdir -p
RM    := rm -rf

COMPILE ?= gcc

TARGET := $(BIN_DIR)/nano-os-test$(VARIANT)

# --- flags -------------------------------------------------------------

# -fno-jump-tables: match the kernel/sim build so switch lowering is the
# same as the shipped-firmware builds.
BASE_CFLAGS := -std=gnu17 -g -fno-jump-tables

ifdef ASAN
  BASE_CFLAGS += -fsanitize=address,undefined -fno-omit-frame-pointer
  LDFLAGS     += -fsanitize=address,undefined
endif

# Mirrors of the per-directory makefiles (paths rebased to test/ as cwd).
KERNEL_CFLAGS := $(BASE_CFLAGS) -rdynamic
KERNEL_INC    := -I../src/user -I../src/include

USER_CFLAGS   := $(BASE_CFLAGS) -rdynamic
USER_INC      := -I../src/kernel -I../src/include

HAL_CFLAGS    := $(BASE_CFLAGS) -DNANO_OS_STRINGS_STRIPPED
HAL_INC       := -I../sim

SIM_CFLAGS    := $(BASE_CFLAGS)
SIM_INC       := -I../sim/hal -I../sim/kernel -I../sim/user

TEST_CFLAGS   := $(BASE_CFLAGS) -Wall -Wextra
# Test code resolves NanoOs headers as "kernel/..." / "user/..." / "hal/..."
# through the sim/ symlink dir, exactly like the kernel sources do - never
# via -I../src/kernel, which would shadow the system <sys/*.h>.
TEST_INC      := -I../sim -Iframework -Iharness -Imocks

# --- sources ---------------------------------------------------------

KERNEL_SRCS := \
    Commands Console Coroutines CTypeSupport Fat32Filesystem Filesystem \
    Hal Link Logger MemoryManager Messages NanoOs Overlay Processes \
    Scheduler SdCard SdCardSpi

USER_SRCS := \
    NanoOsApi NanoOsErrno NanoOsFcntl NanoOsHardware NanoOsLibC NanoOsPwd \
    NanoOsSched NanoOsSignal NanoOsSpawn NanoOsStdio NanoOsSysUtsname \
    NanoOsTermios NanoOsUnistd

# HAL: common wiring + the POSIX low-level implementation.  HalMock reuses
# halPosixImplInit() (overlay-RAM mapping, heap sizing) and the posix
# memory/dio/spi functions; the rest it mocks.  HalPosix.c comes along for
# the file-backed storage layer and is otherwise inert (halPosixInit is
# never called).
HAL_SRCS := HalCommon HalPosix HalPosixImpl

SIM_SRCS := SdCardPosix SdCardPosixImpl

FRAMEWORK_SRCS := NanoOsTest TestMain
HARNESS_SRCS   := KernelTestHarness
MOCK_SRCS      := HalMock MockClock MockTimer MockUart MockBlockDevice
SUITE_SRCS     := test_smoke test_unit_misc test_unit_strtoll \
                  test_kernel_memory test_kernel_hal test_kernel_ipc \
                  test_kernel_scheduler

KERNEL_OBJS := $(addprefix $(OBJ_DIR)/kernel_,$(addsuffix .o,$(KERNEL_SRCS)))
USER_OBJS   := $(addprefix $(OBJ_DIR)/user_,$(addsuffix .o,$(USER_SRCS)))
HAL_OBJS    := $(addprefix $(OBJ_DIR)/hal_,$(addsuffix .o,$(HAL_SRCS)))
SIM_OBJS    := $(addprefix $(OBJ_DIR)/sim_,$(addsuffix .o,$(SIM_SRCS)))
FW_OBJS     := $(addprefix $(OBJ_DIR)/fw_,$(addsuffix .o,$(FRAMEWORK_SRCS)))
HARN_OBJS   := $(addprefix $(OBJ_DIR)/harn_,$(addsuffix .o,$(HARNESS_SRCS)))
MOCK_OBJS   := $(addprefix $(OBJ_DIR)/mock_,$(addsuffix .o,$(MOCK_SRCS)))
SUITE_OBJS  := $(addprefix $(OBJ_DIR)/suite_,$(addsuffix .o,$(SUITE_SRCS)))

ALL_OBJS := $(KERNEL_OBJS) $(USER_OBJS) $(HAL_OBJS) $(SIM_OBJS) \
            $(FW_OBJS) $(HARN_OBJS) $(MOCK_OBJS) $(SUITE_OBJS)

# --- rules ---------------------------------------------------------

.PHONY: all build run clean
all: build run

build: $(TARGET)

run: $(TARGET)
	./$(TARGET)

$(TARGET): $(ALL_OBJS)
	$(MKDIR) "$(BIN_DIR)"
	$(COMPILE) $(LDFLAGS) $(ALL_OBJS) -o $@ -lpthread -ldl

$(OBJ_DIR)/kernel_%.o: ../src/kernel/%.c
	$(MKDIR) "$(OBJ_DIR)"
	$(COMPILE) $(KERNEL_CFLAGS) $(KERNEL_INC) -c $< -o $@

$(OBJ_DIR)/user_%.o: ../src/user/%.c
	$(MKDIR) "$(OBJ_DIR)"
	$(COMPILE) $(USER_CFLAGS) $(USER_INC) -c $< -o $@

$(OBJ_DIR)/hal_%.o: ../src/hal/%.c
	$(MKDIR) "$(OBJ_DIR)"
	$(COMPILE) $(HAL_CFLAGS) $(HAL_INC) -c $< -o $@

$(OBJ_DIR)/sim_%.o: ../sim/%.c
	$(MKDIR) "$(OBJ_DIR)"
	$(COMPILE) $(SIM_CFLAGS) $(SIM_INC) -c $< -o $@

$(OBJ_DIR)/fw_%.o: framework/%.c
	$(MKDIR) "$(OBJ_DIR)"
	$(COMPILE) $(TEST_CFLAGS) $(TEST_INC) -c $< -o $@

$(OBJ_DIR)/harn_%.o: harness/%.c
	$(MKDIR) "$(OBJ_DIR)"
	$(COMPILE) $(TEST_CFLAGS) $(TEST_INC) -c $< -o $@

$(OBJ_DIR)/mock_%.o: mocks/%.c
	$(MKDIR) "$(OBJ_DIR)"
	$(COMPILE) $(TEST_CFLAGS) $(TEST_INC) -c $< -o $@

$(OBJ_DIR)/suite_%.o: suites/%.c
	$(MKDIR) "$(OBJ_DIR)"
	$(COMPILE) $(TEST_CFLAGS) $(TEST_INC) -c $< -o $@

clean:
	$(RM) obj obj-asan $(BIN_DIR)
