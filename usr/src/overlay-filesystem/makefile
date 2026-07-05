include include.mk

LIBRARIES := $(shell ls | grep -Ev 'makefile|.*\.mk|include')

OBJ_DIR = ../../obj
BIN_DIR = ../../bin

.PHONY: all clean overlays

overlays:
	set -e; \
	for library in $(LIBRARIES); do \
		$(MAKE) -C $${library} -f overlay.mk COMPILE=$(COMPILE) \
			LINK=$(LINK) OBJCOPY=$(OBJCOPY) OBJDUMP=$(OBJDUMP) \
			SIZE=$(SIZE) TARGET=$${library}; \
	done

app:
	set -e; \
	for library in $(LIBRARIES); do \
		$(MAKE) -C $${library} -f app.mk COMPILE=$(COMPILE) \
			LINK=$(LINK) OBJCOPY=$(OBJCOPY) OBJDUMP=$(OBJDUMP) \
			SIZE=$(SIZE) TARGET=$${library}; \
	done

clean:
	$(RM) $(OBJ_DIR)/start.o
	for library in $(LIBRARIES); do \
		$(MAKE) -C $${library} -f overlay.mk clean TARGET=$${library}; \
	done
