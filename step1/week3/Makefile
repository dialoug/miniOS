CC ?= cc
CPPFLAGS := -MMD -MP
CFLAGS := -std=c11 -Wall -Wextra -Wpedantic -g

BUILD_DIR := build
TARGET := $(BUILD_DIR)/elf_demo
SOURCES := main.c math_ops.c
OBJECTS := $(SOURCES:%.c=$(BUILD_DIR)/%.o)
DEPS := $(OBJECTS:.o=.d)

.PHONY: all clean preprocess assembly objects broken-link

all: $(TARGET)

$(BUILD_DIR):
	mkdir -p $@

$(BUILD_DIR)/%.i: %.c | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -E $< -o $@

$(BUILD_DIR)/%.s: %.c | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -S $< -o $@

$(BUILD_DIR)/%.o: %.c | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

$(TARGET): $(OBJECTS)
	$(CC) $(OBJECTS) -o $@

preprocess: $(SOURCES:%.c=$(BUILD_DIR)/%.i)

assembly: $(SOURCES:%.c=$(BUILD_DIR)/%.s)

objects: $(OBJECTS)

broken-link: $(BUILD_DIR)/main.o
	$(CC) $< -o $(BUILD_DIR)/broken_link

clean:
	rm -rf $(BUILD_DIR)

-include $(DEPS)
