INCLUDES += -I../../textstore/include

include ../../overlay.mk

# ex needs the eZ80 compiler's smallest code to fit the 8 KB overlay window.
ifneq ($(findstring ez80,$(COMPILE)),)
    CFLAGS += -Oz
endif
