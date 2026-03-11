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
LIFECYCLE_TEST := $(BUILD_DIR)/test_resource_lifecycle
LIFECYCLE_TEST_PADDED := $(BUILD_DIR)/test_resource_lifecycle_padded
IP_RATE_DEMO := $(BUILD_DIR)/ip_rate_limiter_demo
RATE_SERVICE_TEST := $(BUILD_DIR)/test_rate_service
RATE_SERVICE_DEMO := $(BUILD_DIR)/rate_service_demo
RATE_CONFIG_TEST := $(BUILD_DIR)/test_rate_config
RATE_CONFIG_DEMO := $(BUILD_DIR)/rate_config_demo
RATE_DIAGNOSTICS_TEST := $(BUILD_DIR)/test_rate_diagnostics
RATE_DIAGNOSTICS_DEMO := $(BUILD_DIR)/rate_diagnostics_demo
RATE_STATUS_TEST := $(BUILD_DIR)/test_rate_status
RATE_STATUS_DEMO := $(BUILD_DIR)/rate_status_demo
REQUEST_GATEWAY_TEST := $(BUILD_DIR)/test_request_gateway
REQUEST_GATEWAY_DEMO := $(BUILD_DIR)/request_gateway_demo
REQUEST_POLICY_TEST := $(BUILD_DIR)/test_request_policy
WORKLOAD_BENCH := $(BUILD_DIR)/workload_bench
WORKLOAD_BENCH_SOURCES = bench/workload_bench.c $(RATE_SERVICE_SOURCES)
REQUEST_POLICY_DEMO := $(BUILD_DIR)/request_policy_demo
MUTEX_TEST := $(BUILD_DIR)/test_mutex_rate_limiter
MUTEX_COMPARE := $(BUILD_DIR)/mutex_compare
MUTEX_SOURCES = src/mutex_rate_limiter.c $(IP_ATOMIC_SOURCES) $(STATE_MODEL_SOURCES)
MUTEX_TEST_SOURCES = tests/test_mutex_rate_limiter.c src/mutex_rate_limiter.c $(IP_RATE_SOURCES)
MUTEX_COMPARE_SOURCES = bench/mutex_compare.c src/mutex_rate_limiter.c $(IP_RATE_SOURCES)
TRAFFIC_PROFILE_BENCH := $(BUILD_DIR)/realistic_traffic_bench
TRAFFIC_PROFILE_BENCH_SOURCES = bench/realistic_traffic_bench.c src/traffic_workload.c src/mutex_rate_limiter.c $(IP_RATE_SOURCES)
TRAFFIC_WORKLOAD_TEST := $(BUILD_DIR)/test_traffic_workload
CACHELINE_BENCH_PADDED := $(BUILD_DIR)/realistic_traffic_bench_padded
CACHELINE_TEST_COMPACT := $(BUILD_DIR)/test_cacheline_layout_compact
CACHELINE_TEST_PADDED := $(BUILD_DIR)/test_cacheline_layout_padded


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
IP_ATOMIC_SOURCES := src/ip_atomic_table.c src/rate_allocator.c $(IP_PLAN_SOURCES)
IP_ATOMIC_TEST_SOURCES := tests/test_ip_atomic_table.c $(IP_ATOMIC_SOURCES)
IP_ATOMIC_DEMO_SOURCES := examples/ip_atomic_table_demo.c $(IP_ATOMIC_SOURCES)
IP_RATE_SOURCES := src/ip_rate_limiter.c $(IP_ATOMIC_SOURCES) $(LOCKFREE_SOURCES)
IP_RATE_TEST_SOURCES := tests/test_ip_rate_limiter.c $(IP_RATE_SOURCES)
LIFECYCLE_TEST_SOURCES := tests/test_resource_lifecycle.c $(IP_RATE_SOURCES)
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
REQUEST_GATEWAY_SOURCES := src/request_gateway.c $(RATE_SERVICE_SOURCES)
REQUEST_GATEWAY_TEST_SOURCES := tests/test_request_gateway.c $(REQUEST_GATEWAY_SOURCES)
REQUEST_GATEWAY_DEMO_SOURCES := examples/request_gateway_demo.c $(REQUEST_GATEWAY_SOURCES)
REQUEST_POLICY_SOURCES := src/request_policy.c $(REQUEST_GATEWAY_SOURCES)
REQUEST_POLICY_TEST_SOURCES := tests/test_request_policy.c $(REQUEST_POLICY_SOURCES)
REQUEST_POLICY_DEMO_SOURCES := examples/request_policy_demo.c $(REQUEST_POLICY_SOURCES)

.PHONY: traffic-profile-test traffic-profile-sample traffic-profile-report mutex-test mutex-compare-test mutex-compare-report mutex-compare-sample all clean test unit-test properties-test atomic-test atomic-demo state-test state-demo lockfree-test lockfree-demo contention-test ip-plan-test ip-plan-demo ip-atomic-test ip-atomic-demo ip-rate-test ip-rate-demo rate-service-test rate-service-demo rate-config-test rate-config-demo rate-diagnostics-test rate-diagnostics-demo rate-status-test rate-status-demo request-gateway-test request-gateway-demo request-policy-test request-policy-demo workload-bench benchmark-smoke scaling-test scaling-report smoke-test run run-live help

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

$(LIFECYCLE_TEST): $(LIFECYCLE_TEST_SOURCES) include/rate_allocator.h | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -pthread $(LIFECYCLE_TEST_SOURCES) $(LDFLAGS) $(LDLIBS) -pthread -o $@

$(LIFECYCLE_TEST_PADDED): $(LIFECYCLE_TEST_SOURCES) include/rate_allocator.h | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -DRL_CACHELINE_PADDED -pthread $(LIFECYCLE_TEST_SOURCES) $(LDFLAGS) $(LDLIBS) -pthread -o $@

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

$(REQUEST_GATEWAY_TEST): $(REQUEST_GATEWAY_TEST_SOURCES) include/request_gateway.h include/rate_service.h | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -pthread $(REQUEST_GATEWAY_TEST_SOURCES) $(LDFLAGS) $(LDLIBS) -pthread -o $@

$(REQUEST_GATEWAY_DEMO): $(REQUEST_GATEWAY_DEMO_SOURCES) include/request_gateway.h include/rate_service.h | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(REQUEST_GATEWAY_DEMO_SOURCES) $(LDFLAGS) $(LDLIBS) -o $@

$(REQUEST_POLICY_TEST): $(REQUEST_POLICY_TEST_SOURCES) include/request_policy.h include/request_gateway.h | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -pthread $(REQUEST_POLICY_TEST_SOURCES) $(LDFLAGS) $(LDLIBS) -pthread -o $@

$(REQUEST_POLICY_DEMO): $(REQUEST_POLICY_DEMO_SOURCES) include/request_policy.h include/request_gateway.h | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(REQUEST_POLICY_DEMO_SOURCES) $(LDFLAGS) $(LDLIBS) -o $@

$(WORKLOAD_BENCH): $(WORKLOAD_BENCH_SOURCES) include/rate_service.h | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -pthread $(WORKLOAD_BENCH_SOURCES) $(LDFLAGS) $(LDLIBS) -pthread -o $@

$(BUILD_DIR):
	mkdir -p $@

run: $(TARGET)
	./$(TARGET)

run-live: $(TARGET)
	./$(TARGET) --live

unit-test: lifecycle-test lifecycle-test-padded $(BUCKET_TEST) $(CLOCK_TEST)
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

request-gateway-test: $(REQUEST_GATEWAY_TEST)
	./$(REQUEST_GATEWAY_TEST)

request-gateway-demo: $(REQUEST_GATEWAY_DEMO)
	./$(REQUEST_GATEWAY_DEMO)

request-policy-test: $(REQUEST_POLICY_TEST)
	./$(REQUEST_POLICY_TEST)

request-policy-demo: $(REQUEST_POLICY_DEMO)
	./$(REQUEST_POLICY_DEMO)

workload-bench: $(WORKLOAD_BENCH)
	./$(WORKLOAD_BENCH) 1 100000 shared
	./$(WORKLOAD_BENCH) 4 100000 shared
	./$(WORKLOAD_BENCH) 4 100000 sharded

benchmark-smoke: $(WORKLOAD_BENCH)
	./tests/benchmark_smoke.sh ./$(WORKLOAD_BENCH)

scaling-test: $(WORKLOAD_BENCH)
	python3 -m unittest discover -s tests -p 'test_scaling_report.py' -v

scaling-report: $(WORKLOAD_BENCH)
	python3 bench/scaling_report.py --benchmark ./$(WORKLOAD_BENCH) --threads 1,2,4,8 --requests 200000 --repeats 5 --warmups 1

$(MUTEX_TEST): $(MUTEX_TEST_SOURCES) include/mutex_rate_limiter.h | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -pthread $(MUTEX_TEST_SOURCES) $(LDFLAGS) $(LDLIBS) -pthread -o $@

$(MUTEX_COMPARE): $(MUTEX_COMPARE_SOURCES) include/mutex_rate_limiter.h | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -pthread $(MUTEX_COMPARE_SOURCES) $(LDFLAGS) $(LDLIBS) -pthread -o $@

$(TRAFFIC_PROFILE_BENCH): $(TRAFFIC_PROFILE_BENCH_SOURCES) include/traffic_workload.h include/mutex_rate_limiter.h | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -pthread $(TRAFFIC_PROFILE_BENCH_SOURCES) $(LDFLAGS) $(LDLIBS) -pthread -o $@

$(CACHELINE_BENCH_PADDED): $(TRAFFIC_PROFILE_BENCH_SOURCES) include/lockfree_bucket.h | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -DRL_CACHELINE_PADDED -pthread $(TRAFFIC_PROFILE_BENCH_SOURCES) $(LDFLAGS) $(LDLIBS) -pthread -o $@

$(CACHELINE_TEST_COMPACT): tests/test_cacheline_layout.c $(IP_RATE_SOURCES) include/lockfree_bucket.h | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -pthread tests/test_cacheline_layout.c $(IP_RATE_SOURCES) $(LDFLAGS) $(LDLIBS) -pthread -o $@

$(CACHELINE_TEST_PADDED): tests/test_cacheline_layout.c $(IP_RATE_SOURCES) include/lockfree_bucket.h | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -DRL_CACHELINE_PADDED -pthread tests/test_cacheline_layout.c $(IP_RATE_SOURCES) $(LDFLAGS) $(LDLIBS) -pthread -o $@

.PHONY: cacheline-test cacheline-sample cacheline-report
cacheline-test: $(CACHELINE_TEST_COMPACT) $(CACHELINE_TEST_PADDED) $(TRAFFIC_PROFILE_BENCH) $(CACHELINE_BENCH_PADDED)
	./$(CACHELINE_TEST_COMPACT)
	./$(CACHELINE_TEST_PADDED)
	python3 -m unittest discover -s tests -p 'test_cacheline_report.py' -v

cacheline-sample: $(TRAFFIC_PROFILE_BENCH) $(CACHELINE_BENCH_PADDED)
	./$(TRAFFIC_PROFILE_BENCH) lockfree 4 100000 uniform 64 unit 20270105
	./$(CACHELINE_BENCH_PADDED) lockfree 4 100000 uniform 64 unit 20270105

cacheline-report: $(TRAFFIC_PROFILE_BENCH) $(CACHELINE_BENCH_PADDED)
	python3 bench/cacheline_report.py --compact ./$(TRAFFIC_PROFILE_BENCH) --padded ./$(CACHELINE_BENCH_PADDED) --threads 1,4,8 --patterns uniform,skewed --requests 60000 --ips 64 --repeats 3 --warmups 1

$(TRAFFIC_WORKLOAD_TEST): tests/test_traffic_workload.c src/traffic_workload.c include/traffic_workload.h | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) tests/test_traffic_workload.c src/traffic_workload.c $(LDFLAGS) $(LDLIBS) -o $@

traffic-profile-test: $(TRAFFIC_PROFILE_BENCH) $(TRAFFIC_WORKLOAD_TEST)
	./$(TRAFFIC_WORKLOAD_TEST)
	python3 -m unittest discover -s tests -p 'test_traffic_profile_report.py' -v

traffic-profile-sample: $(TRAFFIC_PROFILE_BENCH)
	./$(TRAFFIC_PROFILE_BENCH) mutex 4 30000 skewed 64 mixed
	./$(TRAFFIC_PROFILE_BENCH) lockfree 4 30000 skewed 64 mixed

traffic-profile-report: $(TRAFFIC_PROFILE_BENCH)
	python3 bench/traffic_profile_report.py --benchmark ./$(TRAFFIC_PROFILE_BENCH) --threads 1,4,8 --requests 50000 --ips 64 --repeats 3 --warmups 1

mutex-test: $(MUTEX_TEST)
	./$(MUTEX_TEST)

mutex-compare-test: $(MUTEX_COMPARE)
	./tests/mutex_benchmark_smoke.sh ./$(MUTEX_COMPARE)
	python3 -m unittest discover -s tests -p 'test_mutex_compare_report.py' -v

mutex-compare-sample: $(MUTEX_COMPARE)
	./$(MUTEX_COMPARE) mutex 4 200000 shared
	./$(MUTEX_COMPARE) lockfree 4 200000 shared

mutex-compare-report: $(MUTEX_COMPARE)
	python3 bench/mutex_compare_report.py --benchmark ./$(MUTEX_COMPARE) --threads 1,2,4,8 --requests 200000 --repeats 5 --warmups 1

smoke-test: $(TARGET)
	./tests/smoke.sh ./$(TARGET)

test: api-contract-test unit-test properties-test atomic-test state-test lockfree-test contention-test ip-plan-test ip-atomic-test ip-rate-test rate-service-test rate-config-test rate-diagnostics-test rate-status-test request-gateway-test request-policy-test benchmark-smoke scaling-test mutex-test mutex-compare-test traffic-profile-test cacheline-test latency-test audit-test audit-test-padded audit-runner-test benchmark-cli-test smoke-test ci-config-test static-runner-test

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
	  'make request-gateway-test  Run HTTP-style adapter tests' \
	  'make request-gateway-demo  Run a deterministic 11-request trace' \
	  'make rate-status-demo Show policy classification and counter snapshot' \
	  'make request-policy-test   Test CIDR priority, bypass/block and concurrency' \
	  'make request-policy-demo   Show advanced policy routes' \
	  'make workload-bench  Run 1/4-thread sample throughput workloads' \
	  'make benchmark-smoke Validate benchmark scenario accounting' \
	  'make scaling-test    Validate Phase 19 statistical reporter' \
	  'make scaling-report  Run repeated 1/2/4/8-thread scaling suite' \
	  'make mutex-test     Verify mutex/CAS decision equivalence' \
	  'make mutex-compare-test   Test fair benchmark and parser' \
	  'make mutex-compare-sample  Show one shared 4-thread A/B run' \
	  'make mutex-compare-report  Run repeated paired A/B measurements' \
	  'make traffic-profile-test    Validate deterministic weighted traffic and A/B reports' \
	  'make traffic-profile-sample  Show one skewed mixed-cost A/B example' \
	  'make traffic-profile-report  Repeated hot/uniform/skewed matrix in CSV' \
	  'make cacheline-test   Check compact and 64-byte isolated bucket layouts' \
	  'make cacheline-sample Display matched compact versus padded runs' \
	  'make cacheline-report Paired repeated bucket-layout comparisons' \
	  'make latency-test Validate p50/p95/p99 latency output' \
	  'make benchmark-cli-test Test rate-service benchmark CLI and accounting' \
	  'make audit-test      Run concurrent request and lifecycle invariants' \
	  'make audit-test-padded  Audit padded bucket layout' \
	  'make audit-runner-test Verify TSAN PASS/SKIP/FAIL classification' \
	  'make audit-asan      Run AddressSanitizer + UndefinedBehaviorSanitizer' \
	  'make audit-tsan      Try ThreadSanitizer (77 means unsupported runtime)' \
	  'make test        Run all regression tests through Phase 25' \
	  'make static-check Run GCC and Clang static analyzers (all C files)' \
	  'make api-contract-test Validate bounded input, failure paths and concurrency' \

	  'make static-runner-test Verify analyzer runner fail-closed behavior' \

	  'make clean       Remove generated build artifacts'

# Phase 23: request-level latency, separate from wall-clock throughput.
LATENCY_BENCH := $(BUILD_DIR)/latency_bench
$(LATENCY_BENCH): bench/latency_bench.c src/mutex_rate_limiter.c $(IP_RATE_SOURCES) | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -pthread bench/latency_bench.c src/mutex_rate_limiter.c $(IP_RATE_SOURCES) $(LDFLAGS) $(LDLIBS) -pthread -o $@
.PHONY: latency-test latency-sample
latency-test: $(LATENCY_BENCH)
	python3 tests/test_latency_bench.py ./$(LATENCY_BENCH)
latency-sample: $(LATENCY_BENCH)
	./$(LATENCY_BENCH) mutex 4 20000 shared
	./$(LATENCY_BENCH) lockfree 4 20000 shared

.PHONY: lifecycle-test lifecycle-test-padded
lifecycle-test: $(LIFECYCLE_TEST)
	./$(LIFECYCLE_TEST)

lifecycle-test-padded: $(LIFECYCLE_TEST_PADDED)
	./$(LIFECYCLE_TEST_PADDED)

# Phase 25: sanitizer-focused concurrency audit and previously untracked
# Phase 18 rate-service benchmark integration.
RATE_SERVICE_BENCH := $(BUILD_DIR)/benchmark_rate_service
RATE_SERVICE_BENCH_SOURCES := bench/benchmark_rate_service.c src/traffic_workload.c $(RATE_SERVICE_SOURCES)
AUDIT_TEST := $(BUILD_DIR)/test_concurrency_audit
AUDIT_ASAN := $(BUILD_DIR)/test_concurrency_audit_asan
AUDIT_TSAN := $(BUILD_DIR)/test_concurrency_audit_tsan
AUDIT_PADDED := $(BUILD_DIR)/test_concurrency_audit_padded
AUDIT_SOURCES := tests/test_concurrency_audit.c $(RATE_SERVICE_SOURCES)
AUDIT_SAN_FLAGS := -std=c11 -Wall -Wextra -Wpedantic -Werror -O1 -g -fno-omit-frame-pointer

$(RATE_SERVICE_BENCH): $(RATE_SERVICE_BENCH_SOURCES) include/traffic_workload.h | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -pthread $(RATE_SERVICE_BENCH_SOURCES) $(LDFLAGS) $(LDLIBS) -pthread -o $@

$(AUDIT_TEST): $(AUDIT_SOURCES) include/rate_service.h | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -pthread $(AUDIT_SOURCES) $(LDFLAGS) $(LDLIBS) -pthread -o $@

$(AUDIT_PADDED): $(AUDIT_SOURCES) include/rate_service.h | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -DRL_CACHELINE_PADDED -pthread $(AUDIT_SOURCES) $(LDFLAGS) $(LDLIBS) -pthread -o $@

$(AUDIT_ASAN): $(AUDIT_SOURCES) include/rate_service.h | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(AUDIT_SAN_FLAGS) -fsanitize=address,undefined -pthread $(AUDIT_SOURCES) $(LDFLAGS) -fsanitize=address,undefined $(LDLIBS) -pthread -o $@

$(AUDIT_TSAN): $(AUDIT_SOURCES) include/rate_service.h | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(AUDIT_SAN_FLAGS) -fsanitize=thread -pthread $(AUDIT_SOURCES) $(LDFLAGS) -fsanitize=thread $(LDLIBS) -pthread -o $@

.PHONY: benchmark-cli-test benchmark-cli-sample audit-test audit-test-padded audit-runner-test audit-asan audit-tsan
benchmark-cli-test: $(RATE_SERVICE_BENCH)
	./tests/test_benchmark.sh ./$(RATE_SERVICE_BENCH)

benchmark-cli-sample: $(RATE_SERVICE_BENCH)
	./$(RATE_SERVICE_BENCH) --threads 4 --requests 25000 --ips 64 --pattern skewed --seed 25 --capacity 31 --csv

audit-test: $(AUDIT_TEST)
	./$(AUDIT_TEST)

audit-test-padded: $(AUDIT_PADDED)
	./$(AUDIT_PADDED)

audit-asan: $(AUDIT_ASAN)
	ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 ./$(AUDIT_ASAN)

# TSAN returns exit status 77 when the runtime cannot start. This is NOT a pass.
audit-tsan: $(AUDIT_TSAN)
	./tests/run_tsan.sh ./$(AUDIT_TSAN)

# Exercise the wrapper even on hosts where TSAN runtime support is unavailable.
audit-runner-test:
	./tests/test_tsan_runner.sh

# Phase 26: static checks for CI contract, using Python standard library only.
.PHONY: ci-config-test
ci-config-test:
	python3 -m unittest discover -s tests -p 'test_ci_workflow.py' -v

# Phase 27: reproducible, fail-closed GCC/Clang static source analysis.
.PHONY: static-check static-gcc static-clang static-runner-test
static-gcc:
	python3 tools/static_analysis.py --tool gcc
static-clang:
	python3 tools/static_analysis.py --tool clang
static-check: static-gcc static-clang
static-runner-test:
	python3 -m unittest discover -s tests -p 'test_static_analysis.py' -v

# Phase 28: length-bounded ingress contracts and failure-path regression.
API_CONTRACT_TEST := $(BUILD_DIR)/test_api_contracts
API_CONTRACT_SOURCES := tests/test_api_contracts.c $(REQUEST_GATEWAY_SOURCES)

$(API_CONTRACT_TEST): $(API_CONTRACT_SOURCES) include/request_gateway.h | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -pthread $(API_CONTRACT_SOURCES) $(LDFLAGS) $(LDLIBS) -pthread -o $@

.PHONY: api-contract-test api-contract-asan
api-contract-test: $(API_CONTRACT_TEST)
	./$(API_CONTRACT_TEST)

api-contract-asan:
	$(CC) $(CPPFLAGS) -std=c11 -Wall -Wextra -Wpedantic -Werror -O1 -g -fno-omit-frame-pointer -fsanitize=address,undefined -pthread $(API_CONTRACT_SOURCES) $(LDFLAGS) -fsanitize=address,undefined $(LDLIBS) -pthread -o $(BUILD_DIR)/test_api_contracts_asan
	ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 ./$(BUILD_DIR)/test_api_contracts_asan
