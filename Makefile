BUILD_DIR=build
SOURCE_DIR=src
include $(N64_INST)/include/n64.mk

all: guillem.z64 test.z64

$(BUILD_DIR)/guillem.elf: $(BUILD_DIR)/main.o
guillem.z64: N64_ROM_TITLE = "Guillem Frutem"
$(BUILD_DIR)/test.elf: $(BUILD_DIR)/test.o
test.z64: N64_ROM_TITLE = "Test CPU"

clean:
	rm -rf $(BUILD_DIR) *.z64

-include $(wildcard $(BUILD_DIR)/*.d)
.PHONY: all clean
