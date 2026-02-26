#include "rate_service.h"
#include "monotonic_clock.h"

#include <assert.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#define NS_PER_MS UINT64_C(1000000)

typedef struct {
    _Atomic uint64_t now_ms;
    _Atomic bool fail;
    _Atomic unsigned calls;
} test_clock_t;

static bool fake_now(void *context, uint64_t *now_ms)
{
    test_clock_t *clock = context;
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

static void test_recovery_and_independent_ips(void)
{
    bucket_policy_t policy = {2U, 100U};
    test_clock_t clock = {0};
    rate_service_t service = {0};
    bool found = false;
    uint64_t anchor_ms = 0U;
    uint16_t tokens = 0U;

    atomic_store(&clock.now_ms, 1000U);
    assert(rate_service_init(&service, 8U, &policy, fake_now, &clock));
    assert(rate_service_check(&service, UINT32_C(0), 1U) == RATE_SERVICE_ALLOWED);
    assert(rate_service_check(&service, UINT32_C(0), 1U) == RATE_SERVICE_ALLOWED);
    assert(rate_service_check(&service, UINT32_C(0), 1U) == RATE_SERVICE_DENIED);
    assert(rate_service_check(&service, UINT32_MAX, 1U) == RATE_SERVICE_ALLOWED);
    assert(ip_rate_limiter_snapshot(&service.limiter, UINT32_C(0), &found,
                                    &anchor_ms, &tokens));
    assert(found && anchor_ms == 1000U && tokens == 0U);

    atomic_store(&clock.now_ms, 1099U);
    assert(rate_service_check(&service, UINT32_C(0), 1U) == RATE_SERVICE_DENIED);
    atomic_store(&clock.now_ms, 1100U);
    assert(rate_service_check(&service, UINT32_C(0), 1U) == RATE_SERVICE_ALLOWED);
    assert(rate_service_check(&service, UINT32_C(0), 2U) == RATE_SERVICE_DENIED);
    rate_service_destroy(&service);
    assert(rate_service_check(&service, 0U, 1U) == RATE_SERVICE_INVALID);
    /* The same zero-initialized lifecycle state may be reused after destroy. */
    assert(rate_service_init(&service, 8U, &policy, fake_now, &clock));
    rate_service_destroy(&service);
}

static void test_failures_leave_table_unchanged(void)
{
    bucket_policy_t policy = {3U, 50U};
    test_clock_t clock = {0};
    rate_service_t service = {0};
    unsigned calls;

    atomic_store(&clock.now_ms, 200U);
    atomic_store(&clock.fail, true);
    assert(!rate_service_init(&service, 2U, &policy, fake_now, &clock));
    assert(service.clock == NULL && service.limiter.buckets == NULL);
    atomic_store(&clock.fail, false);
    assert(!rate_service_init(&service, 3U, &policy, fake_now, &clock));
    assert(service.clock == NULL && service.limiter.buckets == NULL);
    assert(rate_service_init(&service, 2U, &policy, fake_now, &clock));
    assert(!rate_service_init(&service, 2U, &policy, fake_now, &clock));
    assert(ip_atomic_table_count_quiescent(&service.limiter.index) == 0U);

    calls = atomic_load(&clock.calls);
    assert(rate_service_check(&service, 1234U, 0U) == RATE_SERVICE_INVALID);
    assert(atomic_load(&clock.calls) == calls);
    assert(ip_atomic_table_count_quiescent(&service.limiter.index) == 0U);

    atomic_store(&clock.fail, true);
    assert(rate_service_check(&service, 1234U, 1U) == RATE_SERVICE_CLOCK_ERROR);
    assert(ip_atomic_table_count_quiescent(&service.limiter.index) == 0U);
    atomic_store(&clock.fail, false);
    atomic_store(&clock.now_ms, BUCKET_STATE_MAX_TIME_MS + 1U);
    assert(rate_service_check(&service, 1234U, 1U) == RATE_SERVICE_CLOCK_ERROR);
    assert(ip_atomic_table_count_quiescent(&service.limiter.index) == 0U);
    atomic_store(&clock.now_ms, 200U);

    assert(rate_service_check(&service, 11U, 3U) == RATE_SERVICE_ALLOWED);
    assert(rate_service_check(&service, 22U, 1U) == RATE_SERVICE_ALLOWED);
    assert(rate_service_check(&service, 33U, 1U) == RATE_SERVICE_TABLE_FULL);
    assert(ip_atomic_table_count_quiescent(&service.limiter.index) == 2U);
    assert(rate_service_check(&service, 11U, 1U) == RATE_SERVICE_DENIED);
    rate_service_destroy(&service);
    rate_service_destroy(&service);
}

static void test_monotonic_adapter(void)
{
    uint64_t before_ns;
    uint64_t after_ns;
    uint64_t now_ms = 17U;
    bucket_policy_t policy = {2U, 50U};
    rate_service_t service = {0};

    assert(!rate_service_monotonic_ms(NULL, NULL));
    assert(monotonic_clock_now_ns(&before_ns) == 0);
    assert(rate_service_monotonic_ms(NULL, &now_ms));
    assert(monotonic_clock_now_ns(&after_ns) == 0);
    assert(before_ns / NS_PER_MS <= now_ms);
    assert(now_ms <= after_ns / NS_PER_MS);
    assert(rate_service_init_monotonic(&service, 8U, &policy));
    assert(rate_service_check(&service, UINT32_C(0x7F000001), 1U) == RATE_SERVICE_ALLOWED);
    rate_service_destroy(&service);
}

typedef struct {
    rate_service_t *service;
    pthread_barrier_t *barrier;
    uint32_t ip;
    unsigned allowed;
    unsigned denied;
} worker_t;

static void *work(void *opaque)
{
    worker_t *arg = opaque;
    size_t i;
    {
        int rc = pthread_barrier_wait(arg->barrier);
        assert(rc == 0 || rc == PTHREAD_BARRIER_SERIAL_THREAD);
    }
    for (i = 0U; i < 5000U; ++i) {
        rate_service_result_t result = rate_service_check(arg->service,
                                                            arg->ip, 1U);
        if (result == RATE_SERVICE_ALLOWED) {
            ++arg->allowed;
        } else {
            assert(result == RATE_SERVICE_DENIED);
            ++arg->denied;
        }
    }
    return NULL;
}

static void test_concurrent_real_api(void)
{
    enum { THREADS = 12 };
    pthread_t threads[THREADS];
    worker_t workers[THREADS] = {0};
    pthread_barrier_t barrier;
    bucket_policy_t policy = {37U, 1000U};
    test_clock_t clock = {0};
    rate_service_t service = {0};
    size_t i;
    unsigned sum_a = 0U;
    unsigned sum_b = 0U;
    unsigned denied = 0U;

    atomic_store(&clock.now_ms, 5000U); /* no refill across workers */
    assert(rate_service_init(&service, 128U, &policy, fake_now, &clock));
    assert(pthread_barrier_init(&barrier, NULL, THREADS + 1U) == 0);
    for (i = 0U; i < THREADS; ++i) {
        workers[i].service = &service;
        workers[i].barrier = &barrier;
        workers[i].ip = (i & 1U) == 0U ? 1U : UINT32_MAX;
        assert(pthread_create(&threads[i], NULL, work, &workers[i]) == 0);
    }
    {
        int rc = pthread_barrier_wait(&barrier);
        assert(rc == 0 || rc == PTHREAD_BARRIER_SERIAL_THREAD);
    }
    for (i = 0U; i < THREADS; ++i) {
        assert(pthread_join(threads[i], NULL) == 0);
        if ((i & 1U) == 0U) {
            sum_a += workers[i].allowed;
        } else {
            sum_b += workers[i].allowed;
        }
        denied += workers[i].denied;
    }
    assert(sum_a == 37U && sum_b == 37U);
    assert(denied == THREADS * 5000U - 74U);
    assert(ip_atomic_table_count_quiescent(&service.limiter.index) == 2U);
    assert(pthread_barrier_destroy(&barrier) == 0);
    rate_service_destroy(&service);
    printf("clock-aware contention: %u requests, exactly %u admitted\n",
           THREADS * 5000U, sum_a + sum_b);
}

int main(void)
{
    test_recovery_and_independent_ips();
    test_failures_leave_table_unchanged();
    test_monotonic_adapter();
    test_concurrent_real_api();
    puts("rate service tests passed");
    return 0;
}
