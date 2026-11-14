include ../include.mk

OBJ_DIR = ../../../obj
BIN_DIR = ../../../bin

LIBRARIES := $(shell ls | grep -Ev 'makefile|.*\.mk')
OBJECTS := $(addprefix $(OBJ_DIR)/$(TARGET)/, $(addsuffix .o, $(LIBRARIES))) \

.PHONY: clean

all: $(BIN_DIR)/$(TARGET)

$(BIN_DIR)/$(TARGET): $(OBJECTS)
	$(MKDIR) "$(BIN_DIR)"
	$(COMPILE) $(LDFLAGS) $(OBJECTS) $(LINKS) -o $@

$(OBJ_DIR)/$(TARGET)/%.o: ./%
	$(MAKE) -C $* -f library.mk COMPILE=$(COMPILE) LINK=$(LINK) \
		OBJCOPY=$(OBJCOPY) OBJDUMP=$(OBJDUMP) SIZE=$(SIZE) \
		TARGET=$(TARGET) LIBRARY=$*

clean:
	$(RM) $(OBJ_DIR)/$(TARGET)
	$(RM) $(BIN_DIR)/$(TARGET)
	for library in $(LIBRARIES); do \
		$(MAKE) -C $${library} -f library.mk clean; \
	done
