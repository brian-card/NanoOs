include include.mk

COMMANDS := $(shell ls | grep -Ev 'makefile|.*\.mk')

# wm needs a display driver, so it is built only when the build that runs
# this makefile says the platform has one with WINDOW_MANAGER=1.
BUILD_COMMANDS := $(COMMANDS)
ifneq ($(WINDOW_MANAGER),1)
    BUILD_COMMANDS := $(filter-out wm,$(COMMANDS))
endif

OBJ_DIR = ../../obj
BIN_DIR = ../../bin

.PHONY: all clean overlays

overlays:
	set -e; \
	for command in $(BUILD_COMMANDS); do \
		$(MAKE) -C $${command} -f overlay.mk COMPILE=$(COMPILE) \
			LINK=$(LINK) OBJCOPY=$(OBJCOPY) OBJDUMP=$(OBJDUMP) \
			SIZE=$(SIZE) TARGET=$${command}; \
	done

app:
	set -e; \
	for command in $(BUILD_COMMANDS); do \
		$(MAKE) -C $${command} -f app.mk COMPILE=$(COMPILE) \
			LINK=$(LINK) OBJCOPY=$(OBJCOPY) OBJDUMP=$(OBJDUMP) \
			SIZE=$(SIZE) TARGET=$${command}; \
	done

clean:
	$(RM) $(OBJ_DIR)/start.o
	for command in $(COMMANDS); do \
		$(MAKE) -C $${command} -f overlay.mk clean TARGET=$${command}; \
	done
