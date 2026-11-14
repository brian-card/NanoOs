include include.mk

COMMANDS := $(shell ls | grep -Ev 'makefile|.*\.mk')

OBJ_DIR = ../../obj
BIN_DIR = ../../bin

.PHONY: all clean overlays

overlays:
	set -e; \
	for command in $(COMMANDS); do \
		$(MAKE) -C $${command} -f overlay.mk COMPILE=$(COMPILE) \
			LINK=$(LINK) OBJCOPY=$(OBJCOPY) OBJDUMP=$(OBJDUMP) \
			SIZE=$(SIZE) TARGET=$${command}; \
	done

app:
	set -e; \
	for command in $(COMMANDS); do \
		$(MAKE) -C $${command} -f app.mk COMPILE=$(COMPILE) \
			LINK=$(LINK) OBJCOPY=$(OBJCOPY) OBJDUMP=$(OBJDUMP) \
			SIZE=$(SIZE) TARGET=$${command}; \
	done

clean:
	$(RM) $(OBJ_DIR)/start.o
	for command in $(COMMANDS); do \
		$(MAKE) -C $${command} -f overlay.mk clean TARGET=$${command}; \
	done
