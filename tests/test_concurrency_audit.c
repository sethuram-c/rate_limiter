/* Phase 25: concurrent rate-service contract audit.
 * Designed for GCC/Clang, ASan/UBSan and (where supported) ThreadSanitizer.
 * Shared immutable config is initialized before threads launch; all per-worker
 * counters are thread-owned until pthread_join establishes completion.
 */
#include "rate_service.h"

#include <assert.h>
#include <inttypes.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#define AUDIT_THREADS 8U
#define AUDIT_REQUESTS 20000U
#define AUDIT_IPS 16U
#define AUDIT_TOKENS 31U
#define AUDIT_ANCHOR_MS UINT64_C(9000)
#define AUDIT_IP_BASE UINT32_C(0xc0000200)

typedef struct {
    uint64_t now_ms; /* immutable after worker creation */
} audit_clock_t;

typedef struct {
    rate_service_t *service;
    pthread_barrier_t *start;
    unsigned worker_id;
    uint64_t allowed;
    uint64_t denied;
    uint64_t invalid;
    uint64_t failures;
} audit_worker_t;

static bool fixed_clock(void *context, uint64_t *out_ms)
{
    const audit_clock_t *clock = context;
    if (clock == NULL || out_ms == NULL) {
        return false;
    }
    *out_ms = clock->now_ms;
    return true;
}

static void *worker_main(void *context)
{
    audit_worker_t *worker = context;
    unsigned iteration;
    int barrier_rc = pthread_barrier_wait(worker->start);
    assert(barrier_rc == 0 || barrier_rc == PTHREAD_BARRIER_SERIAL_THREAD);

    for (iteration = 0U; iteration < AUDIT_REQUESTS; ++iteration) {
        uint32_t ip = AUDIT_IP_BASE +
            (uint32_t)((iteration + worker->worker_id * 17U) % AUDIT_IPS);
        uint32_t amount = iteration % 5U == 0U ? 0U : 1U;
        rate_service_result_t decision =
            rate_service_check(worker->service, ip, amount);
        switch (decision) {
        case RATE_SERVICE_ALLOWED: ++worker->allowed; break;
        case RATE_SERVICE_DENIED: ++worker->denied; break;
        case RATE_SERVICE_INVALID: ++worker->invalid; break;
        default: ++worker->failures; break;
        }
    }
    return NULL;
}

static void audit_parallel_admission_and_stats(void)
{
    pthread_t threads[AUDIT_THREADS];
    audit_worker_t workers[AUDIT_THREADS] = {0};
    pthread_barrier_t barrier;
    rate_service_t service = {0};
    rate_service_stats_t stats = {0};
    audit_clock_t clock = {AUDIT_ANCHOR_MS};
    bucket_policy_t policy = {AUDIT_TOKENS, UINT64_C(1000000)};
    uint64_t allowed = 0U, denied = 0U, invalid = 0U, failures = 0U;
    unsigned i;
    const uint64_t attempts = AUDIT_THREADS * (uint64_t)AUDIT_REQUESTS;
    const uint64_t expected_invalid = AUDIT_THREADS * (uint64_t)(AUDIT_REQUESTS / 5U);
    const uint64_t expected_allowed = AUDIT_IPS * (uint64_t)AUDIT_TOKENS;

    assert(rate_service_init(&service, 64U, &policy, fixed_clock, &clock));
    assert(pthread_barrier_init(&barrier, NULL, AUDIT_THREADS + 1U) == 0);

    for (i = 0U; i < AUDIT_THREADS; ++i) {
        workers[i].service = &service;
        workers[i].start = &barrier;
        workers[i].worker_id = i;
        assert(pthread_create(&threads[i], NULL, worker_main, &workers[i]) == 0);
    }
    {
        int rc = pthread_barrier_wait(&barrier);
        assert(rc == 0 || rc == PTHREAD_BARRIER_SERIAL_THREAD);
    }
    for (i = 0U; i < AUDIT_THREADS; ++i) {
        assert(pthread_join(threads[i], NULL) == 0);
        allowed += workers[i].allowed;
        denied += workers[i].denied;
        invalid += workers[i].invalid;
        failures += workers[i].failures;
    }
    assert(pthread_barrier_destroy(&barrier) == 0);

    assert(failures == 0U);
    assert(allowed == expected_allowed);
    assert(invalid == expected_invalid);
    assert(denied == attempts - expected_invalid - expected_allowed);
    assert(rate_service_get_stats(&service, &stats));
    assert(stats.allowed == allowed && stats.denied == denied);
    assert(stats.invalid == invalid);
    assert(stats.table_full == 0U && stats.clock_error == 0U);
    assert(stats.allowed + stats.denied + stats.invalid == attempts);
    assert(ip_atomic_table_count_quiescent(&service.limiter.index) == AUDIT_IPS);

    for (i = 0U; i < AUDIT_IPS; ++i) {
        bool found = false;
        uint64_t anchor_ms = 0U;
        uint16_t remaining = UINT16_MAX;
        assert(ip_rate_limiter_snapshot(&service.limiter, AUDIT_IP_BASE + i,
                                        &found, &anchor_ms, &remaining));
        assert(found && anchor_ms == AUDIT_ANCHOR_MS && remaining == 0U);
    }
    rate_service_destroy(&service); /* only AFTER pthread_join */
    rate_service_destroy(&service); /* quiescent repeated destroy */
    printf("concurrency audit: %" PRIu64 " requests, %" PRIu64
           " admitted, %" PRIu64 " invalid, %" PRIu64 " denied\n",
           attempts, allowed, invalid, denied);
}

static void audit_full_table_and_reuse(void)
{
    audit_clock_t clock = {AUDIT_ANCHOR_MS};
    bucket_policy_t policy = {1U, 1000U};
    rate_service_t service = {0};
    rate_service_stats_t stats;
    unsigned round;
    for (round = 0U; round < 2U; ++round) {
        assert(rate_service_init(&service, 2U, &policy, fixed_clock, &clock));
        assert(rate_service_check(&service, 1U, 1U) == RATE_SERVICE_ALLOWED);
        assert(rate_service_check(&service, 2U, 1U) == RATE_SERVICE_ALLOWED);
        assert(rate_service_check(&service, 3U, 1U) == RATE_SERVICE_TABLE_FULL);
        assert(rate_service_check(&service, 1U, 1U) == RATE_SERVICE_DENIED);
        assert(rate_service_get_stats(&service, &stats));
        assert(stats.allowed == 2U && stats.denied == 1U &&
               stats.table_full == 1U);
        rate_service_destroy(&service);
    }
}

int main(void)
{
    audit_parallel_admission_and_stats();
    audit_full_table_and_reuse();
    puts("phase 25 concurrency audit passed");
    return 0;
}
