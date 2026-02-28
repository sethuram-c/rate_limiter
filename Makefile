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
CONTENTION_TEST := $(BUILD_DIR)/test_contention
IP_PLAN_TEST := $(BUILD_DIR)/test_ip_table_plan
IP_PLAN_DEMO := $(BUILD_DIR)/ip_table_plan_demo
IP_ATOMIC_TEST := $(BUILD_DIR)/test_ip_atomic_table
IP_ATOMIC_DEMO := $(BUILD_DIR)/ip_atomic_table_demo
IP_RATE_TEST := $(BUILD_DIR)/test_ip_rate_limiter
IP_RATE_DEMO := $(BUILD_DIR)/ip_rate_limiter_demo
RATE_SERVICE_TEST := $(BUILD_DIR)/test_rate_service
RATE_SERVICE_DEMO := $(BUILD_DIR)/rate_service_demo
RATE_CONFIG_TEST := $(BUILD_DIR)/test_rate_config
RATE_CONFIG_DEMO := $(BUILD_DIR)/rate_config_demo
RATE_DIAGNOSTICS_TEST := $(BUILD_DIR)/test_rate_diagnostics
RATE_DIAGNOSTICS_DEMO := $(BUILD_DIR)/rate_diagnostics_demo
RATE_STATUS_TEST := $(BUILD_DIR)/test_rate_status
RATE_STATUS_DEMO := $(BUILD_DIR)/rate_status_demo

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
CONTENTION_TEST_SOURCES := tests/test_contention.c tests/linearizability_check.c $(LOCKFREE_SOURCES)
IP_PLAN_SOURCES := src/ip_table_plan.c
IP_PLAN_TEST_SOURCES := tests/test_ip_table_plan.c $(IP_PLAN_SOURCES)
IP_PLAN_DEMO_SOURCES := examples/ip_table_plan_demo.c $(IP_PLAN_SOURCES)
IP_ATOMIC_SOURCES := src/ip_atomic_table.c $(IP_PLAN_SOURCES)
IP_ATOMIC_TEST_SOURCES := tests/test_ip_atomic_table.c $(IP_ATOMIC_SOURCES)
IP_ATOMIC_DEMO_SOURCES := examples/ip_atomic_table_demo.c $(IP_ATOMIC_SOURCES)
IP_RATE_SOURCES := src/ip_rate_limiter.c $(IP_ATOMIC_SOURCES) $(LOCKFREE_SOURCES)
IP_RATE_TEST_SOURCES := tests/test_ip_rate_limiter.c $(IP_RATE_SOURCES)
IP_RATE_DEMO_SOURCES := examples/ip_rate_limiter_demo.c $(IP_RATE_SOURCES)
RATE_SERVICE_SOURCES := src/rate_service.c src/rate_config.c src/monotonic_clock.c $(IP_RATE_SOURCES)
RATE_SERVICE_TEST_SOURCES := tests/test_rate_service.c $(RATE_SERVICE_SOURCES)
RATE_SERVICE_DEMO_SOURCES := examples/rate_service_demo.c $(RATE_SERVICE_SOURCES)
RATE_CONFIG_SOURCES := $(RATE_SERVICE_SOURCES)
RATE_CONFIG_TEST_SOURCES := tests/test_rate_config.c $(RATE_CONFIG_SOURCES)
RATE_CONFIG_DEMO_SOURCES := examples/rate_config_demo.c $(RATE_CONFIG_SOURCES)
RATE_DIAGNOSTICS_TEST_SOURCES := tests/test_rate_diagnostics.c $(RATE_SERVICE_SOURCES)
RATE_DIAGNOSTICS_DEMO_SOURCES := examples/rate_diagnostics_demo.c $(RATE_SERVICE_SOURCES)
RATE_STATUS_TEST_SOURCES := tests/test_rate_status.c $(RATE_SERVICE_SOURCES)
RATE_STATUS_DEMO_SOURCES := examples/rate_status_demo.c $(RATE_SERVICE_SOURCES)

.PHONY: all clean test unit-test properties-test atomic-test atomic-demo state-test state-demo lockfree-test lockfree-demo contention-test ip-plan-test ip-plan-demo ip-atomic-test ip-atomic-demo ip-rate-test ip-rate-demo rate-service-test rate-service-demo rate-config-test rate-config-demo rate-diagnostics-test rate-diagnostics-demo rate-status-test rate-status-demo smoke-test run run-live help

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

$(CONTENTION_TEST): $(CONTENTION_TEST_SOURCES) tests/linearizability_check.h include/lockfree_bucket.h include/bucket_state.h | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -pthread $(CONTENTION_TEST_SOURCES) $(LDFLAGS) $(LDLIBS) -pthread -o $@

$(IP_PLAN_TEST): $(IP_PLAN_TEST_SOURCES) include/ip_table_plan.h | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(IP_PLAN_TEST_SOURCES) $(LDFLAGS) $(LDLIBS) -o $@

$(IP_PLAN_DEMO): $(IP_PLAN_DEMO_SOURCES) include/ip_table_plan.h | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(IP_PLAN_DEMO_SOURCES) $(LDFLAGS) $(LDLIBS) -o $@

$(IP_ATOMIC_TEST): $(IP_ATOMIC_TEST_SOURCES) include/ip_atomic_table.h include/ip_table_plan.h | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -pthread $(IP_ATOMIC_TEST_SOURCES) $(LDFLAGS) $(LDLIBS) -pthread -o $@

$(IP_ATOMIC_DEMO): $(IP_ATOMIC_DEMO_SOURCES) include/ip_atomic_table.h include/ip_table_plan.h | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(IP_ATOMIC_DEMO_SOURCES) $(LDFLAGS) $(LDLIBS) -o $@

$(IP_RATE_TEST): $(IP_RATE_TEST_SOURCES) include/ip_rate_limiter.h include/ip_atomic_table.h include/lockfree_bucket.h | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -pthread $(IP_RATE_TEST_SOURCES) $(LDFLAGS) $(LDLIBS) -pthread -o $@

$(IP_RATE_DEMO): $(IP_RATE_DEMO_SOURCES) include/ip_rate_limiter.h | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(IP_RATE_DEMO_SOURCES) $(LDFLAGS) $(LDLIBS) -o $@

$(RATE_SERVICE_TEST): $(RATE_SERVICE_TEST_SOURCES) include/rate_service.h include/ip_rate_limiter.h include/monotonic_clock.h | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -pthread $(RATE_SERVICE_TEST_SOURCES) $(LDFLAGS) $(LDLIBS) -pthread -o $@

$(RATE_SERVICE_DEMO): $(RATE_SERVICE_DEMO_SOURCES) include/rate_service.h include/ip_rate_limiter.h include/monotonic_clock.h | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(RATE_SERVICE_DEMO_SOURCES) $(LDFLAGS) $(LDLIBS) -o $@

$(RATE_CONFIG_TEST): $(RATE_CONFIG_TEST_SOURCES) include/rate_config.h include/rate_service.h | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -pthread $(RATE_CONFIG_TEST_SOURCES) $(LDFLAGS) $(LDLIBS) -pthread -o $@

$(RATE_CONFIG_DEMO): $(RATE_CONFIG_DEMO_SOURCES) include/rate_config.h include/rate_service.h | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(RATE_CONFIG_DEMO_SOURCES) $(LDFLAGS) $(LDLIBS) -o $@

$(RATE_DIAGNOSTICS_TEST): $(RATE_DIAGNOSTICS_TEST_SOURCES) include/rate_service.h include/ip_rate_limiter.h include/lockfree_bucket.h | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -pthread $(RATE_DIAGNOSTICS_TEST_SOURCES) $(LDFLAGS) $(LDLIBS) -pthread -o $@

$(RATE_DIAGNOSTICS_DEMO): $(RATE_DIAGNOSTICS_DEMO_SOURCES) include/rate_service.h | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(RATE_DIAGNOSTICS_DEMO_SOURCES) $(LDFLAGS) $(LDLIBS) -o $@

$(RATE_STATUS_TEST): $(RATE_STATUS_TEST_SOURCES) include/rate_service.h | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -pthread $(RATE_STATUS_TEST_SOURCES) $(LDFLAGS) $(LDLIBS) -pthread -o $@

$(RATE_STATUS_DEMO): $(RATE_STATUS_DEMO_SOURCES) include/rate_service.h | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(RATE_STATUS_DEMO_SOURCES) $(LDFLAGS) $(LDLIBS) -o $@

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

contention-test: $(CONTENTION_TEST)
	./$(CONTENTION_TEST)

ip-plan-test: $(IP_PLAN_TEST)
	./$(IP_PLAN_TEST)

ip-plan-demo: $(IP_PLAN_DEMO)
	./$(IP_PLAN_DEMO)

ip-atomic-test: $(IP_ATOMIC_TEST)
	./$(IP_ATOMIC_TEST)

ip-atomic-demo: $(IP_ATOMIC_DEMO)
	./$(IP_ATOMIC_DEMO)

ip-rate-test: $(IP_RATE_TEST)
	./$(IP_RATE_TEST)

ip-rate-demo: $(IP_RATE_DEMO)
	./$(IP_RATE_DEMO)

rate-service-test: $(RATE_SERVICE_TEST)
	./$(RATE_SERVICE_TEST)

rate-service-demo: $(RATE_SERVICE_DEMO)
	./$(RATE_SERVICE_DEMO)
	./$(RATE_SERVICE_DEMO) --live

rate-config-test: $(RATE_CONFIG_TEST)
	./$(RATE_CONFIG_TEST)

rate-config-demo: $(RATE_CONFIG_DEMO)
	./$(RATE_CONFIG_DEMO)

rate-diagnostics-test: $(RATE_DIAGNOSTICS_TEST)
	./$(RATE_DIAGNOSTICS_TEST)

rate-diagnostics-demo: $(RATE_DIAGNOSTICS_DEMO)
	./$(RATE_DIAGNOSTICS_DEMO)

rate-status-test: $(RATE_STATUS_TEST)
	./$(RATE_STATUS_TEST)

rate-status-demo: $(RATE_STATUS_DEMO)
	./$(RATE_STATUS_DEMO)

smoke-test: $(TARGET)
	./tests/smoke.sh ./$(TARGET)

test: unit-test properties-test atomic-test state-test lockfree-test contention-test ip-plan-test ip-atomic-test ip-rate-test rate-service-test rate-config-test rate-diagnostics-test rate-status-test smoke-test

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
	  'make contention-test Profile CAS retries + check concurrent histories' \
	  'make ip-plan-test  Test Phase 9 sequential hash-table model' \
	  'make ip-plan-demo  Show collision/duplicate behavior' \
	  'make ip-atomic-test Test CAS per-IP insertion under contention' \
	  'make ip-atomic-demo Show atomic publication demo' \
	  'make ip-rate-test Test per-IP bucket integration and contention' \
	  'make ip-rate-demo Show per-IP independent admission decisions' \
	  'make rate-service-test  Test clock-aware request API' \
	  'make rate-service-demo  Show injected and live monotonic clock processing' \
	  'make rate-config-test  Test resource planning and defensive configuration' \
	  'make rate-config-demo  Show exact memory planning and rejection' \
	  'make rate-diagnostics-test  Test decision-aligned CAS diagnostics' \
	  'make rate-diagnostics-demo  Show retry-hint policy behavior' \
	  'make rate-status-test Test operational status and atomic counters' \
	  'make rate-status-demo Show policy classification and counter snapshot' \
	  'make test        Run all Phase 15 tests' \
	  'make clean       Remove generated build artifacts'
