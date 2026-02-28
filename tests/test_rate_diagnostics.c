#include "rate_service.h"

#include <assert.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

/* Fixed clock is an atomic variable, so tests never depend on sleeping. */
typedef struct {
    _Atomic uint64_t now_ms;
    _Atomic bool fail;
    _Atomic unsigned calls;
} clock_state_t;

static bool fake_clock(void *context, uint64_t *now_ms)
{
    clock_state_t *clock = context;
    if (clock == NULL || now_ms == NULL) {
        return false;
    }
    (void)atomic_fetch_add_explicit(&clock->calls, 1U, memory_order_relaxed);
    if (atomic_load_explicit(&clock->fail, memory_order_relaxed)) {
        return false;
    }
    *now_ms = atomic_load_explicit(&clock->now_ms, memory_order_relaxed);
    return true;
}

static rate_service_diagnostic_t check(rate_service_t *s, uint32_t ip,
                                        uint32_t amount)
{
    rate_service_diagnostic_t report = {0};
    assert(rate_service_check_detailed(s, ip, amount, &report));
    return report;
}

static void test_precise_committed_state_and_retry(void)
{
    bucket_policy_t policy = {3U, 100U};
    clock_state_t clock = {0};
    rate_service_t service = {0};
    rate_service_diagnostic_t report;
    unsigned i;

    atomic_store(&clock.now_ms, 1000U);
    assert(rate_service_init(&service, 2U, &policy, fake_clock, &clock));
    for (i = 0U; i < 3U; ++i) {
        report = check(&service, UINT32_C(0), 1U);
        assert(report.result == RATE_SERVICE_ALLOWED);
        assert(report.has_bucket_state && !report.retry_possible);
        assert(report.remaining_tokens == (uint16_t)(2U - i));
        assert(report.refill_anchor_ms == 1000U);
        assert(report.sampled_now_ms == 1000U);
        assert(report.capacity == 3U && report.refill_interval_ms == 100U);
    }
    report = check(&service, UINT32_C(0), 2U);
    assert(report.result == RATE_SERVICE_DENIED && report.retry_possible);
    assert(report.retry_after_ms == 200U && report.remaining_tokens == 0U);

    atomic_store(&clock.now_ms, 1050U);
    report = check(&service, UINT32_C(0), 2U);
    assert(report.result == RATE_SERVICE_DENIED && report.retry_possible);
    assert(report.refill_anchor_ms == 1000U && report.retry_after_ms == 150U);

    atomic_store(&clock.now_ms, 1100U);
    report = check(&service, UINT32_C(0), 2U);
    assert(report.result == RATE_SERVICE_DENIED && report.retry_possible);
    assert(report.remaining_tokens == 1U && report.refill_anchor_ms == 1100U);
    assert(report.retry_after_ms == 100U);

    atomic_store(&clock.now_ms, 1200U);
    report = check(&service, UINT32_C(0), 2U);
    assert(report.result == RATE_SERVICE_ALLOWED && !report.retry_possible);
    assert(report.remaining_tokens == 0U && report.refill_anchor_ms == 1200U);

    report = check(&service, UINT32_C(0), 4U);
    assert(report.result == RATE_SERVICE_DENIED && report.has_bucket_state);
    assert(!report.retry_possible && report.retry_after_ms == 0U);

    /* Key 0 is valid. Key UINT32_MAX is valid too. Table capacity is 2. */
    assert(check(&service, UINT32_MAX, 1U).result == RATE_SERVICE_ALLOWED);
    report = check(&service, 99U, 1U);
    assert(report.result == RATE_SERVICE_TABLE_FULL);
    assert(!report.has_bucket_state && !report.retry_possible);
    assert(report.remaining_tokens == 0U && report.refill_anchor_ms == 0U);
    rate_service_destroy(&service);
}

static void test_lower_layer_cas_reporting(void)
{
    bucket_policy_t policy = {2U, 100U};
    lf_bucket_t bucket;
    bool allowed = false;
    uint64_t committed = UINT64_MAX;
    uint64_t anchor = 0U;
    uint16_t tokens = 0U;
    ip_rate_limiter_t limiter = {0};
    ip_rate_report_t report = {0};

    assert(lf_bucket_init(&bucket, &policy, 500U));
    assert(!lf_bucket_try_consume_observed(&bucket, 0U, 500U,
                                            &allowed, &committed));
    assert(committed == UINT64_MAX);
    assert(lf_bucket_try_consume_observed(&bucket, 1U, 500U,
                                           &allowed, &committed));
    assert(allowed);
    bucket_state_unpack(committed, &anchor, &tokens);
    assert(anchor == 500U && tokens == 1U);

    assert(ip_rate_limiter_init(&limiter, 2U, &policy, 500U));
    assert(!ip_rate_limiter_check_report(&limiter, 1U, 1U, 500U, NULL));
    assert(ip_atomic_table_count_quiescent(&limiter.index) == 0U);
    assert(ip_rate_limiter_check_report(&limiter, 1U, 0U, 500U, &report));
    assert(report.decision == IP_RATE_INVALID && !report.valid_state);
    assert(ip_atomic_table_count_quiescent(&limiter.index) == 0U);
    assert(ip_rate_limiter_check_report(&limiter, 1U, 1U, 500U, &report));
    assert(report.decision == IP_RATE_ALLOWED && report.valid_state);
    bucket_state_unpack(report.committed_state, &anchor, &tokens);
    assert(anchor == 500U && tokens == 1U && report.policy.capacity == 2U);
    ip_rate_limiter_destroy(&limiter);
}

static void test_no_side_effects_on_errors(void)
{
    bucket_policy_t policy = {2U, 20U};
    clock_state_t clock = {0};
    rate_service_t service = {0};
    rate_service_diagnostic_t out = {0};
    unsigned calls;

    atomic_store(&clock.now_ms, 1U);
    assert(rate_service_init(&service, 2U, &policy, fake_clock, &clock));
    calls = atomic_load(&clock.calls);
    assert(!rate_service_check_detailed(&service, 12U, 1U, NULL));
    assert(atomic_load(&clock.calls) == calls);
    assert(ip_atomic_table_count_quiescent(&service.limiter.index) == 0U);

    out = check(&service, 12U, 0U);
    assert(out.result == RATE_SERVICE_INVALID && !out.has_bucket_state);
    assert(atomic_load(&clock.calls) == calls);
    assert(ip_atomic_table_count_quiescent(&service.limiter.index) == 0U);

    atomic_store(&clock.fail, true);
    out = check(&service, 12U, 1U);
    assert(out.result == RATE_SERVICE_CLOCK_ERROR && !out.has_bucket_state);
    assert(ip_atomic_table_count_quiescent(&service.limiter.index) == 0U);
    atomic_store(&clock.fail, false);

    atomic_store(&clock.now_ms, BUCKET_STATE_MAX_TIME_MS + 1U);
    out = check(&service, 12U, 1U);
    assert(out.result == RATE_SERVICE_CLOCK_ERROR && !out.has_bucket_state);
    assert(ip_atomic_table_count_quiescent(&service.limiter.index) == 0U);
    rate_service_destroy(&service);
    out = check(&service, 12U, 1U);
    assert(out.result == RATE_SERVICE_INVALID && !out.has_bucket_state);
}

static void test_unreachable_retry_boundary(void)
{
    bucket_policy_t policy = {2U, BUCKET_STATE_MAX_TIME_MS};
    clock_state_t clock = {0};
    rate_service_t service = {0};
    rate_service_diagnostic_t report;

    atomic_store(&clock.now_ms, BUCKET_STATE_MAX_TIME_MS - 1U);
    assert(rate_service_init(&service, 4U, &policy, fake_clock, &clock));
    assert(check(&service, 5U, 2U).result == RATE_SERVICE_ALLOWED);
    report = check(&service, 5U, 1U);
    assert(report.result == RATE_SERVICE_DENIED && report.has_bucket_state);
    assert(!report.retry_possible && report.retry_after_ms == 0U);
    rate_service_destroy(&service);
}

enum { THREADS = 8, CALLS_PER_THREAD = 4000 };
typedef struct {
    rate_service_t *service;
    pthread_barrier_t *barrier;
    uint32_t ip;
    unsigned allowed;
    unsigned denied;
} worker_t;

static void *worker_fn(void *opaque)
{
    worker_t *w = opaque;
    unsigned i;
    int rc = pthread_barrier_wait(w->barrier);
    assert(rc == 0 || rc == PTHREAD_BARRIER_SERIAL_THREAD);
    for (i = 0U; i < CALLS_PER_THREAD; ++i) {
        rate_service_diagnostic_t report = check(w->service, w->ip, 1U);
        assert(report.has_bucket_state && report.capacity == 17U);
        assert(report.remaining_tokens <= 17U);
        assert(report.refill_anchor_ms == 1000U);
        if (report.result == RATE_SERVICE_ALLOWED) {
            ++w->allowed;
        } else {
            assert(report.result == RATE_SERVICE_DENIED);
            assert(report.retry_possible && report.retry_after_ms == 1000U);
            ++w->denied;
        }
    }
    return NULL;
}

static void test_concurrent_diagnostics(void)
{
    clock_state_t clock = {0};
    rate_service_t service = {0};
    bucket_policy_t policy = {17U, 1000U};
    pthread_t threads[THREADS];
    worker_t workers[THREADS] = {0};
    pthread_barrier_t barrier;
    unsigned allowed_a = 0U, allowed_b = 0U, denied = 0U;
    unsigned i;

    atomic_store(&clock.now_ms, 1000U);
    assert(rate_service_init(&service, 8U, &policy, fake_clock, &clock));
    assert(pthread_barrier_init(&barrier, NULL, THREADS + 1U) == 0);
    for (i = 0U; i < THREADS; ++i) {
        workers[i].service = &service;
        workers[i].barrier = &barrier;
        workers[i].ip = i % 2U == 0U ? 1U : 2U;
        assert(pthread_create(&threads[i], NULL, worker_fn, &workers[i]) == 0);
    }
    {
        int rc = pthread_barrier_wait(&barrier);
        assert(rc == 0 || rc == PTHREAD_BARRIER_SERIAL_THREAD);
    }
    for (i = 0U; i < THREADS; ++i) {
        assert(pthread_join(threads[i], NULL) == 0);
        if (i % 2U == 0U) {
            allowed_a += workers[i].allowed;
        } else {
            allowed_b += workers[i].allowed;
        }
        denied += workers[i].denied;
    }
    assert(allowed_a == 17U && allowed_b == 17U);
    assert(denied == THREADS * CALLS_PER_THREAD - 34U);
    assert(pthread_barrier_destroy(&barrier) == 0);
    rate_service_destroy(&service);
    printf("request diagnostics: %u concurrent calls, exactly %u admitted\n",
           THREADS * CALLS_PER_THREAD, allowed_a + allowed_b);
}

int main(void)
{
    test_precise_committed_state_and_retry();
    test_lower_layer_cas_reporting();
    test_no_side_effects_on_errors();
    test_unreachable_retry_boundary();
    test_concurrent_diagnostics();
    puts("rate diagnostics tests passed");
    return 0;
}
