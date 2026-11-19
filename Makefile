BUILD_DIR=build
SOURCE_DIR=src
include $(N64_INST)/include/n64.mk
CFLAGS += -Wno-error

OBJS = $(BUILD_DIR)/main.o

all: guillem.z64

$(BUILD_DIR)/guillem.elf: $(OBJS)
guillem.z64: N64_ROM_TITLE = "Guillem Frutem"

clean:
	rm -rf $(BUILD_DIR) *.z64

-include $(wildcard $(BUILD_DIR)/*.d)
.PHONY: all clean
