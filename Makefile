CC ?= cc
CPPFLAGS := -Iinclude
CFLAGS ?= -std=c11 -Wall -Wextra -Wpedantic -Werror -O2 -g
LDFLAGS ?=
LDLIBS ?=

BUILD_DIR := build
TARGET := $(BUILD_DIR)/ratelimiter
SOURCES := src/main.c
OBJECTS := $(SOURCES:src/%.c=$(BUILD_DIR)/%.o)

.PHONY: all clean test run help

all: $(TARGET)

$(TARGET): $(OBJECTS)
	$(CC) $(LDFLAGS) $^ $(LDLIBS) -o $@

$(BUILD_DIR)/%.o: src/%.c | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

$(BUILD_DIR):
	mkdir -p $@

run: $(TARGET)
	./$(TARGET)

test: $(TARGET)
	./tests/smoke.sh ./$(TARGET)

clean:
	rm -rf $(BUILD_DIR)

help:
	@printf '%s\n' \
	  'make        Build the project' \
	  'make run    Build and run the scaffold executable' \
	  'make test   Build and run the Phase 1 smoke test' \
	  'make clean  Remove generated build artifacts'
