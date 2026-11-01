include include.mk

LIBRARIES := $(shell ls | grep -Ev 'makefile|.*\.mk|include')

# vdp.drv drives Agon Light 2 hardware only.
BUILD_LIBRARIES := $(LIBRARIES)
ifeq ($(COMPILE),arm-none-eabi-gcc)
    BUILD_LIBRARIES := $(filter-out vdp.drv,$(LIBRARIES))
endif

OBJ_DIR = ../../obj
BIN_DIR = ../../bin

.PHONY: all clean overlays

overlays:
	set -e; \
	for library in $(BUILD_LIBRARIES); do \
		$(MAKE) -C $${library} -f overlay.mk COMPILE=$(COMPILE) \
			LINK=$(LINK) OBJCOPY=$(OBJCOPY) OBJDUMP=$(OBJDUMP) \
			SIZE=$(SIZE) TARGET=$${library}; \
	done

app:
	set -e; \
	for library in $(BUILD_LIBRARIES); do \
		$(MAKE) -C $${library} -f app.mk COMPILE=$(COMPILE) \
			LINK=$(LINK) OBJCOPY=$(OBJCOPY) OBJDUMP=$(OBJDUMP) \
			SIZE=$(SIZE) TARGET=$${library}; \
	done

clean:
	$(RM) $(OBJ_DIR)/start.o
	for library in $(LIBRARIES); do \
		$(MAKE) -C $${library} -f overlay.mk clean TARGET=$${library}; \
	done
