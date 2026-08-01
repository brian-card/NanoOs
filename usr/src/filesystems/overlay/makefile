include include.mk

OBJ_DIR := ../../../obj/overlay-filesystem
BIN_DIR := ../../../filesystem/overlay-filesystem

# Every overlay slot is the union of common/<slot> (driver-agnostic wrapper
# code, always present) and $(FILESYSTEM)/<slot> (the selected driver's
# implementation, present only for slots that need one).  Slot directory
# names are plain numbers, so this glob can't accidentally pick up
# non-numbered directories like common/include.
SLOTS := $(sort $(notdir $(wildcard common/[0-9]*)) \
    $(notdir $(wildcard $(FILESYSTEM)/[0-9]*)))

.PHONY: all clean overlays $(SLOTS)

overlays: $(SLOTS)

$(SLOTS):
	$(MAKE) -f slot.mk SLOT=$@ COMPILE="$(COMPILE)" LINK="$(LINK)" \
		OBJCOPY="$(OBJCOPY)" OBJDUMP="$(OBJDUMP)" SIZE="$(SIZE)" \
		LINKER_SCRIPT="$(LINKER_SCRIPT)" FILESYSTEM="$(FILESYSTEM)" \
		EXTRA_INCLUDES="$(EXTRA_INCLUDES)" EXTRA_LINKS="$(EXTRA_LINKS)"

clean:
	$(RM) $(OBJ_DIR) $(BIN_DIR)
