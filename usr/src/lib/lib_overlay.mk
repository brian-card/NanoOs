include ../include.mk

OBJ_DIR = ../../../obj
BIN_DIR = ../../../bin

OVERLAYS := $(shell ls | grep -Ev 'makefile|.*\.mk')

.PHONY: all clean $(OVERLAYS)

all: $(OVERLAYS)

$(OVERLAYS): ./%:
	$(MAKE) -C $@ -f overlay.mk COMPILE=$(COMPILE) LINK=$(LINK) \
		OBJCOPY=$(OBJCOPY) OBJDUMP=$(OBJDUMP) SIZE=$(SIZE) \
		TARGET=$(TARGET) OVERLAY=$@

clean:
	$(RM) $(OBJ_DIR)/$(TARGET)
	$(RM) $(BIN_DIR)/$(TARGET)
	for overlay in $(OVERLAYS); do \
		$(MAKE) -C $${overlay} -f overlay.mk clean; \
	done
