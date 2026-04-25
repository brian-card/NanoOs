SOURCES := commands lib

.PHONY: all clean overlays

overlays:
	set -e; \
	for source in $(SOURCES); do \
		$(MAKE) -C $${source} COMPILE=$(COMPILE) LINK=$(LINK) \
			OBJCOPY=$(OBJCOPY) OBJDUMP=$(OBJDUMP) SIZE=$(SIZE) \
			LINKER_SCRIPT=$(LINKER_SCRIPT) overlays; \
	done

clean:
	for source in $(SOURCES); do \
		$(MAKE) -C $${source} clean; \
	done
