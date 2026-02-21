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
ATOMIC_TEST := $(BUILD_DIR)/test_atomic_lab
ATOMIC_DEMO := $(BUILD_DIR)/atomic_lab_demo
STATE_TEST := $(BUILD_DIR)/test_bucket_state
STATE_DEMO := $(BUILD_DIR)/bucket_state_demo
LOCKFREE_TEST := $(BUILD_DIR)/test_lockfree_bucket
LOCKFREE_DEMO := $(BUILD_DIR)/lockfree_bucket_demo

SOURCES := src/main.c src/token_bucket.c src/monotonic_clock.c
OBJECTS := $(SOURCES:src/%.c=$(BUILD_DIR)/%.o)
BUCKET_TEST_SOURCES := tests/test_token_bucket.c src/token_bucket.c
CLOCK_TEST_SOURCES := tests/test_monotonic_clock.c src/monotonic_clock.c
PROPERTIES_TEST_SOURCES := tests/test_bucket_properties.c src/token_bucket.c
ATOMIC_LAB_SOURCES := src/atomic_lab.c
ATOMIC_TEST_SOURCES := tests/test_atomic_lab.c $(ATOMIC_LAB_SOURCES)
ATOMIC_DEMO_SOURCES := examples/atomic_lab_demo.c $(ATOMIC_LAB_SOURCES)
STATE_MODEL_SOURCES := src/bucket_state.c
STATE_TEST_SOURCES := tests/test_bucket_state.c $(STATE_MODEL_SOURCES) src/token_bucket.c
STATE_DEMO_SOURCES := examples/bucket_state_demo.c $(STATE_MODEL_SOURCES)
LOCKFREE_SOURCES := src/lockfree_bucket.c $(STATE_MODEL_SOURCES)
LOCKFREE_TEST_SOURCES := tests/test_lockfree_bucket.c $(LOCKFREE_SOURCES)
LOCKFREE_DEMO_SOURCES := examples/lockfree_bucket_demo.c $(LOCKFREE_SOURCES)

.PHONY: all clean test unit-test properties-test atomic-test atomic-demo state-test state-demo lockfree-test lockfree-demo smoke-test run run-live help

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

$(ATOMIC_TEST): $(ATOMIC_TEST_SOURCES) include/atomic_lab.h | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -pthread $(ATOMIC_TEST_SOURCES) $(LDFLAGS) $(LDLIBS) -pthread -o $@

$(ATOMIC_DEMO): $(ATOMIC_DEMO_SOURCES) include/atomic_lab.h | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(ATOMIC_DEMO_SOURCES) $(LDFLAGS) $(LDLIBS) -o $@

$(STATE_TEST): $(STATE_TEST_SOURCES) include/bucket_state.h include/token_bucket.h | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(STATE_TEST_SOURCES) $(LDFLAGS) $(LDLIBS) -o $@

$(STATE_DEMO): $(STATE_DEMO_SOURCES) include/bucket_state.h | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(STATE_DEMO_SOURCES) $(LDFLAGS) $(LDLIBS) -o $@

$(LOCKFREE_TEST): $(LOCKFREE_TEST_SOURCES) include/lockfree_bucket.h include/bucket_state.h | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -pthread $(LOCKFREE_TEST_SOURCES) $(LDFLAGS) $(LDLIBS) -pthread -o $@

$(LOCKFREE_DEMO): $(LOCKFREE_DEMO_SOURCES) include/lockfree_bucket.h include/bucket_state.h | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LOCKFREE_DEMO_SOURCES) $(LDFLAGS) $(LDLIBS) -o $@

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

atomic-test: $(ATOMIC_TEST)
	./$(ATOMIC_TEST)

atomic-demo: $(ATOMIC_DEMO)
	./$(ATOMIC_DEMO)

state-test: $(STATE_TEST)
	./$(STATE_TEST)

state-demo: $(STATE_DEMO)
	./$(STATE_DEMO)

lockfree-test: $(LOCKFREE_TEST)
	./$(LOCKFREE_TEST)

lockfree-demo: $(LOCKFREE_DEMO)
	./$(LOCKFREE_DEMO)

smoke-test: $(TARGET)
	./tests/smoke.sh ./$(TARGET)

test: unit-test properties-test atomic-test state-test lockfree-test smoke-test

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
	  'make atomic-test Run pthread contention + release/acquire tests' \
	  'make atomic-demo Run the bounded-CAS learning demo' \
	  'make state-test  Validate packed-state transitions against reference' \
	  'make state-demo  Display packed state and planned updates' \
	  'make lockfree-test Run pthread CAS bucket tests' \
	  'make lockfree-demo Show live atomic bucket decisions' \
	  'make test        Run all Phase 7 tests' \
	  'make clean       Remove generated build artifacts'
