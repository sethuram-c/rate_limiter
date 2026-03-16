#include "ip_rate_limiter.h"
#include "rate_service.h"
#include "request_gateway.h"

#include <assert.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#define TEST_WORKERS 8U
#define TEST_CALLS 5000U

static bool fixed_clock(void *context, uint64_t *now_ms)
{
    _Atomic uint64_t *clock = context;
    if (clock == NULL || now_ms == NULL) {
        return false;
    }
    *now_ms = atomic_load_explicit(clock, memory_order_relaxed);
    return true;
}

static void test_never_admissible_requests_do_not_fill_index(void)
{
    const bucket_policy_t policy = {2U, 100U};
    ip_rate_limiter_t limiter = {0};
    ip_rate_report_t report = {0};
    bool found = true;
    uint64_t anchor_ms = UINT64_MAX;
    uint16_t tokens = UINT16_MAX;
    uint32_t key;

    assert(ip_rate_limiter_init(&limiter, 2U, &policy, 1000U));
    for (key = 1U; key <= 10000U; ++key) {
        assert(ip_rate_limiter_check_report(&limiter, key, UINT32_MAX,
                                            1000U, &report));
        assert(report.decision == IP_RATE_DENIED && !report.valid_state);
    }
    assert(ip_atomic_table_count_quiescent(&limiter.index) == 0U);
    assert(ip_rate_limiter_snapshot(&limiter, 1U, &found,
                                    &anchor_ms, &tokens));
    assert(!found && anchor_ms == UINT64_MAX && tokens == UINT16_MAX);

    assert(ip_rate_limiter_check(&limiter, 1U, 1U, 1000U) == IP_RATE_ALLOWED);
    assert(ip_rate_limiter_check(&limiter, 2U, 1U, 1000U) == IP_RATE_ALLOWED);
    assert(ip_atomic_table_count_quiescent(&limiter.index) == 2U);
    assert(ip_rate_limiter_check(&limiter, 3U, 1U, 1000U) == IP_RATE_TABLE_FULL);
    /* For a never-admissible amount, DENIED takes precedence over table-full. */
    assert(ip_rate_limiter_check(&limiter, 3U, 3U, 1000U) == IP_RATE_DENIED);

    /* Existing IPs still use the CAS path: refill and committed diagnostics. */
    assert(ip_rate_limiter_check_report(&limiter, 1U, UINT32_MAX,
                                        1200U, &report));
    assert(report.decision == IP_RATE_DENIED && report.valid_state);
    assert(report.policy.capacity == 2U);
    assert(ip_rate_limiter_snapshot(&limiter, 1U, &found,
                                    &anchor_ms, &tokens));
    assert(found && anchor_ms == 1200U && tokens == 2U);
    assert(ip_atomic_table_count_quiescent(&limiter.index) == 2U);

    assert(ip_rate_limiter_check_report(&limiter, 3U, 0U, 1200U, &report));
    assert(report.decision == IP_RATE_INVALID && !report.valid_state);
    ip_rate_limiter_destroy(&limiter);
}

typedef struct {
    ip_rate_limiter_t *limiter;
    pthread_barrier_t *barrier;
    uint32_t seed;
} thread_arg_t;

static void *impossible_worker(void *arg_ptr)
{
    thread_arg_t *arg = arg_ptr;
    unsigned i;
    int rc = pthread_barrier_wait(arg->barrier);
    assert(rc == 0 || rc == PTHREAD_BARRIER_SERIAL_THREAD);
    for (i = 0U; i < TEST_CALLS; ++i) {
        uint32_t ip = arg->seed + i;
        assert(ip_rate_limiter_check(arg->limiter, ip, 4U, 1000U) ==
               IP_RATE_DENIED);
    }
    return NULL;
}

static void test_concurrent_oversized_requests(void)
{
    const bucket_policy_t policy = {3U, 100U};
    ip_rate_limiter_t limiter = {0};
    pthread_barrier_t barrier;
    pthread_t threads[TEST_WORKERS];
    thread_arg_t args[TEST_WORKERS];
    unsigned i;

    assert(ip_rate_limiter_init(&limiter, 8U, &policy, 1000U));
    assert(pthread_barrier_init(&barrier, NULL, TEST_WORKERS) == 0);
    for (i = 0U; i < TEST_WORKERS; ++i) {
        args[i].limiter = &limiter;
        args[i].barrier = &barrier;
        args[i].seed = 100000U + i * TEST_CALLS;
        assert(pthread_create(&threads[i], NULL, impossible_worker,
                              &args[i]) == 0);
    }
    for (i = 0U; i < TEST_WORKERS; ++i) {
        assert(pthread_join(threads[i], NULL) == 0);
    }
    assert(pthread_barrier_destroy(&barrier) == 0);
    assert(ip_atomic_table_count_quiescent(&limiter.index) == 0U);
    assert(ip_rate_limiter_check(&limiter, 42U, 3U, 1000U) == IP_RATE_ALLOWED);
    ip_rate_limiter_destroy(&limiter);
    printf("oversized flood: %u denied, 0 slots claimed before valid traffic\n",
           TEST_WORKERS * TEST_CALLS);
}

static void test_service_and_gateway_diagnostics(void)
{
    const bucket_policy_t policy = {2U, 100U};
    rate_service_t service = {0};
    rate_service_diagnostic_t report = {0};
    request_gateway_response_t gateway = {0};
    rate_service_stats_t stats = {0};
    _Atomic uint64_t now_ms = 1000U;

    assert(rate_service_init(&service, 2U, &policy, fixed_clock, &now_ms));
    assert(rate_service_check_detailed(&service, 100U, UINT32_MAX, &report));
    assert(report.result == RATE_SERVICE_DENIED && !report.has_bucket_state &&
           !report.retry_possible && report.retry_after_ms == 0U);
    assert(request_gateway_handle(&service, "192.0.2.1", 3U, &gateway));
    assert(gateway.forwarded && gateway.http_status == 429 &&
           !gateway.has_remaining && !gateway.has_retry_hint);
    assert(ip_atomic_table_count_quiescent(&service.limiter.index) == 0U);

    assert(rate_service_check(&service, 100U, 1U) == RATE_SERVICE_ALLOWED);
    assert(rate_service_check_detailed(&service, 100U, 3U, &report));
    assert(report.result == RATE_SERVICE_DENIED && report.has_bucket_state &&
           !report.retry_possible);
    assert(ip_atomic_table_count_quiescent(&service.limiter.index) == 1U);

    assert(rate_service_get_stats(&service, &stats));
    assert(stats.denied == 3U && stats.allowed == 1U &&
           stats.invalid == 0U && stats.table_full == 0U &&
           stats.clock_error == 0U);
    rate_service_destroy(&service);
}

int main(void)
{
    test_never_admissible_requests_do_not_fill_index();
    test_concurrent_oversized_requests();
    test_service_and_gateway_diagnostics();
    puts("phase 33 impossible-request audit passed");
    return 0;
}
