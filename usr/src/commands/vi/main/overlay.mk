SOURCES := \
    ../../../start.c \

INCLUDES += -I../../../lib/ex/include -I../../../lib/textstore/include \
    -I../../../lib/screen/include

include ../../overlay.mk

# vi needs the eZ80 compiler's smallest code to fit the 8 KB overlay window.
ifneq ($(findstring ez80,$(COMPILE)),)
    CFLAGS += -Oz
endif
