BUILD_DIR=build
SOURCE_DIR=src
include $(N64_INST)/include/n64.mk

all: guillem.z64 test.z64 hello.z64 test32.z64

$(BUILD_DIR)/guillem.elf: $(BUILD_DIR)/main.o
guillem.z64: N64_ROM_TITLE = "Guillem Frutem"
$(BUILD_DIR)/test.elf: $(BUILD_DIR)/test.o
test.z64: N64_ROM_TITLE = "Test CPU"
$(BUILD_DIR)/hello.elf: $(BUILD_DIR)/hello.o
hello.z64: N64_ROM_TITLE = "Hola N64"
$(BUILD_DIR)/test32.elf: $(BUILD_DIR)/test32.o
test32.z64: N64_ROM_TITLE = "Test 32bit"

clean:
	rm -rf $(BUILD_DIR) *.z64

-include $(wildcard $(BUILD_DIR)/*.d)
.PHONY: all clean
