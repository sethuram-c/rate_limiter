CC ?= cc
CPPFLAGS := -Iinclude
CFLAGS ?= -std=c11 -Wall -Wextra -Wpedantic -Werror -O2 -g
LDFLAGS ?=
LDLIBS ?=

BUILD_DIR := build
TARGET := $(BUILD_DIR)/ratelimiter
TEST_TARGET := $(BUILD_DIR)/test_token_bucket

SOURCES := src/main.c src/token_bucket.c
OBJECTS := $(SOURCES:src/%.c=$(BUILD_DIR)/%.o)
TEST_SOURCES := tests/test_token_bucket.c src/token_bucket.c

.PHONY: all clean test unit-test smoke-test run help

all: $(TARGET)

$(TARGET): $(OBJECTS)
	$(CC) $(LDFLAGS) $^ $(LDLIBS) -o $@

$(BUILD_DIR)/%.o: src/%.c | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

$(TEST_TARGET): $(TEST_SOURCES) include/token_bucket.h | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(TEST_SOURCES) $(LDFLAGS) $(LDLIBS) -o $@

$(BUILD_DIR):
	mkdir -p $@

run: $(TARGET)
	./$(TARGET)

unit-test: $(TEST_TARGET)
	./$(TEST_TARGET)

smoke-test: $(TARGET)
	./tests/smoke.sh ./$(TARGET)

test: unit-test smoke-test

clean:
	rm -rf $(BUILD_DIR)

help:
	@printf '%s\n' \
	  'make             Build the demo program' \
	  'make run         Build and run the deterministic token-bucket demo' \
	  'make unit-test   Run focused C unit tests for the token bucket' \
	  'make smoke-test  Run the shell-level behavior check' \
	  'make test        Run all Phase 2 tests' \
	  'make clean       Remove generated build artifacts'
