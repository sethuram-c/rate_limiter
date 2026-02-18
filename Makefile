CC ?= cc
# clock_gettime and CLOCK_MONOTONIC require POSIX feature exposure with -std=c11.
CPPFLAGS := -D_POSIX_C_SOURCE=200809L -Iinclude
CFLAGS ?= -std=c11 -Wall -Wextra -Wpedantic -Werror -O2 -g
LDFLAGS ?=
LDLIBS ?=

BUILD_DIR := build
TARGET := $(BUILD_DIR)/ratelimiter
BUCKET_TEST := $(BUILD_DIR)/test_token_bucket
CLOCK_TEST := $(BUILD_DIR)/test_monotonic_clock
PROPERTIES_TEST := $(BUILD_DIR)/test_bucket_properties

SOURCES := src/main.c src/token_bucket.c src/monotonic_clock.c
OBJECTS := $(SOURCES:src/%.c=$(BUILD_DIR)/%.o)
BUCKET_TEST_SOURCES := tests/test_token_bucket.c src/token_bucket.c
CLOCK_TEST_SOURCES := tests/test_monotonic_clock.c src/monotonic_clock.c
PROPERTIES_TEST_SOURCES := tests/test_bucket_properties.c src/token_bucket.c

.PHONY: all clean test unit-test properties-test smoke-test run run-live help

all: $(TARGET)

$(TARGET): $(OBJECTS)
	$(CC) $(LDFLAGS) $^ $(LDLIBS) -o $@

$(BUILD_DIR)/main.o: include/token_bucket.h include/monotonic_clock.h
$(BUILD_DIR)/token_bucket.o: include/token_bucket.h
$(BUILD_DIR)/monotonic_clock.o: include/monotonic_clock.h

$(BUILD_DIR)/%.o: src/%.c | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

$(BUCKET_TEST): $(BUCKET_TEST_SOURCES) include/token_bucket.h | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(BUCKET_TEST_SOURCES) $(LDFLAGS) $(LDLIBS) -o $@

$(CLOCK_TEST): $(CLOCK_TEST_SOURCES) include/monotonic_clock.h | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(CLOCK_TEST_SOURCES) $(LDFLAGS) $(LDLIBS) -o $@

$(PROPERTIES_TEST): $(PROPERTIES_TEST_SOURCES) include/token_bucket.h | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(PROPERTIES_TEST_SOURCES) $(LDFLAGS) $(LDLIBS) -o $@

$(BUILD_DIR):
	mkdir -p $@

run: $(TARGET)
	./$(TARGET)

run-live: $(TARGET)
	./$(TARGET) --live

unit-test: $(BUCKET_TEST) $(CLOCK_TEST)
	./$(BUCKET_TEST)
	./$(CLOCK_TEST)

properties-test: $(PROPERTIES_TEST)
	./$(PROPERTIES_TEST)

smoke-test: $(TARGET)
	./tests/smoke.sh ./$(TARGET)

test: unit-test properties-test smoke-test

clean:
	rm -rf $(BUILD_DIR)

help:
	@printf '%s\n' \
	  'make             Build the demo program' \
	  'make run         Run the repeatable, simulated-time token-bucket demo' \
	  'make run-live    Run a real Linux CLOCK_MONOTONIC sample demo' \
	  'make unit-test   Run token-bucket and monotonic-clock C tests' \
	  'make properties-test   Run deterministic reference-model checks' \
	  'make smoke-test  Check the CLI behavior' \
	  'make test        Run all Phase 4 tests' \
	  'make clean       Remove generated build artifacts'
