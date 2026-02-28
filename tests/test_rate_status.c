#include "rate_service.h"

#include <assert.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

typedef struct {
    uint64_t value;
    bool fail;
} fake_clock_t;

static bool clock_read(void *opaque, uint64_t *now_ms)
{
    const fake_clock_t *clock = opaque;
    if (clock == NULL || clock->fail || now_ms == NULL) {
        return false;
    }
    *now_ms = clock->value;
    return true;
}

static uint64_t total(const rate_service_stats_t *s)
{
    return s->allowed + s->denied + s->table_full +
           s->invalid + s->clock_error;
}

static void test_classification(void)
{
    assert(rate_service_classify(RATE_SERVICE_ALLOWED) == RATE_STATUS_ADMIT);
    assert(rate_service_classify(RATE_SERVICE_DENIED) == RATE_STATUS_THROTTLED);
    assert(rate_service_classify(RATE_SERVICE_TABLE_FULL) ==
           RATE_STATUS_CAPACITY_EXHAUSTED);
    assert(rate_service_classify(RATE_SERVICE_INVALID) == RATE_STATUS_BAD_REQUEST);
    assert(rate_service_classify(RATE_SERVICE_CLOCK_ERROR) ==
           RATE_STATUS_CLOCK_UNAVAILABLE);
    assert(rate_service_classify((rate_service_result_t)100) == RATE_STATUS_UNKNOWN);
    assert(strcmp(rate_service_result_name(RATE_SERVICE_ALLOWED), "ALLOWED") == 0);
    assert(strcmp(rate_service_result_name(RATE_SERVICE_DENIED), "DENIED") == 0);
    assert(strcmp(rate_service_result_name(RATE_SERVICE_TABLE_FULL), "TABLE_FULL") == 0);
    assert(strcmp(rate_service_result_name(RATE_SERVICE_INVALID), "INVALID") == 0);
    assert(strcmp(rate_service_result_name(RATE_SERVICE_CLOCK_ERROR), "CLOCK_ERROR") == 0);
    assert(strcmp(rate_service_result_name((rate_service_result_t)100), "UNKNOWN") == 0);
}

static void test_error_accounting(void)
{
    rate_service_t service = {0};
    fake_clock_t clock = {1000U, false};
    bucket_policy_t policy = {2U, 100U};
    rate_service_diagnostic_t report;
    rate_service_stats_t stats = {99U, 99U, 99U, 99U, 99U};

    assert(!rate_service_get_stats(&service, &stats));
    assert(stats.allowed == 99U);
    assert(!rate_service_get_stats(NULL, &stats));
    assert(rate_service_check(&service, 1U, 1U) == RATE_SERVICE_INVALID);
    assert(rate_service_init(&service, 2U, &policy, clock_read, &clock));
    assert(rate_service_get_stats(&service, &stats));
    assert(total(&stats) == 0U);

    assert(rate_service_check(&service, 1U, 1U) == RATE_SERVICE_ALLOWED);
    assert(rate_service_check_detailed(&service, 1U, 1U, &report));
    assert(report.result == RATE_SERVICE_ALLOWED);
    assert(rate_service_check(&service, 1U, 1U) == RATE_SERVICE_DENIED);
    assert(rate_service_check_detailed(&service, 1U, 1U, &report));
    assert(report.result == RATE_SERVICE_DENIED);
    assert(rate_service_check(&service, 2U, 1U) == RATE_SERVICE_ALLOWED);
    assert(rate_service_check(&service, 3U, 1U) == RATE_SERVICE_TABLE_FULL);
    assert(rate_service_check_detailed(&service, 3U, 1U, &report));
    assert(report.result == RATE_SERVICE_TABLE_FULL);
    assert(rate_service_check(&service, 2U, 0U) == RATE_SERVICE_INVALID);
    assert(rate_service_check_detailed(&service, 2U, 0U, &report));
    assert(report.result == RATE_SERVICE_INVALID);
    assert(!rate_service_check_detailed(&service, 1U, 1U, NULL));
    clock.fail = true;
    assert(rate_service_check(&service, 2U, 1U) == RATE_SERVICE_CLOCK_ERROR);
    assert(rate_service_check_detailed(&service, 2U, 1U, &report));
    assert(report.result == RATE_SERVICE_CLOCK_ERROR);

    assert(rate_service_get_stats(&service, &stats));
    assert(stats.allowed == 3U);
    assert(stats.denied == 2U);
    assert(stats.table_full == 2U);
    assert(stats.invalid == 2U);
    assert(stats.clock_error == 2U);
    assert(total(&stats) == 11U);
    assert(!rate_service_get_stats(&service, NULL));
    rate_service_destroy(&service);
    assert(!rate_service_get_stats(&service, &stats));
    assert(rate_service_init(&service, 2U, &policy, clock_read, &clock) == false);
    clock.fail = false;
    assert(rate_service_init(&service, 2U, &policy, clock_read, &clock));
    assert(rate_service_get_stats(&service, &stats));
    assert(total(&stats) == 0U);
    rate_service_destroy(&service);
    puts("operational status: all five result counters validated");
}

enum { THREADS = 8, REQUESTS_PER_THREAD = 20000, IP_COUNT = 4 };

typedef struct {
    rate_service_t *service;
    pthread_barrier_t *start;
    unsigned index;
    uint32_t allowed;
    uint32_t denied;
} worker_arg_t;

static void *worker(void *opaque)
{
    worker_arg_t *arg = opaque;
    unsigned i;
    {
        int rc = pthread_barrier_wait(arg->start);
        assert(rc == 0 || rc == PTHREAD_BARRIER_SERIAL_THREAD);
    }
    for (i = 0U; i < REQUESTS_PER_THREAD; ++i) {
        const uint32_t ip = UINT32_C(0xC0000201) + ((i + arg->index) % IP_COUNT);
        rate_service_result_t result;
        if ((i & 1U) == 0U) {
            rate_service_diagnostic_t report;
            assert(rate_service_check_detailed(arg->service, ip, 1U, &report));
            result = report.result;
        } else {
            result = rate_service_check(arg->service, ip, 1U);
        }
        if (result == RATE_SERVICE_ALLOWED) {
            ++arg->allowed;
        } else {
            assert(result == RATE_SERVICE_DENIED);
            ++arg->denied;
        }
    }
    return NULL;
}

static void test_mixed_api_contention(void)
{
    rate_service_t service = {0};
    const fake_clock_t clock = {5000U, false};
    const bucket_policy_t policy = {37U, 100U};
    pthread_barrier_t barrier;
    pthread_t ids[THREADS];
    worker_arg_t args[THREADS] = {0};
    rate_service_stats_t stats;
    unsigned i;
    unsigned total_allowed = 0U;
    unsigned total_denied = 0U;

    assert(rate_service_init(&service, IP_COUNT, &policy, clock_read,
                             (void *)&clock));
    assert(pthread_barrier_init(&barrier, NULL, THREADS) == 0);
    for (i = 0U; i < THREADS; ++i) {
        args[i].service = &service;
        args[i].start = &barrier;
        args[i].index = i;
        assert(pthread_create(&ids[i], NULL, worker, &args[i]) == 0);
    }
    for (i = 0U; i < THREADS; ++i) {
        assert(pthread_join(ids[i], NULL) == 0);
        total_allowed += args[i].allowed;
        total_denied += args[i].denied;
    }
    assert(pthread_barrier_destroy(&barrier) == 0);
    assert(total_allowed == IP_COUNT * 37U);
    assert(total_denied == THREADS * REQUESTS_PER_THREAD - total_allowed);
    assert(rate_service_get_stats(&service, &stats));
    assert(stats.allowed == total_allowed);
    assert(stats.denied == total_denied);
    assert(stats.table_full == 0U);
    assert(stats.invalid == 0U);
    assert(stats.clock_error == 0U);
    assert(total(&stats) == THREADS * REQUESTS_PER_THREAD);
    rate_service_destroy(&service);
    printf("operational status: %u requests, exactly %u admitted across %u IPs\n",
           THREADS * REQUESTS_PER_THREAD, total_allowed, IP_COUNT);
}

int main(void)
{
    test_classification();
    test_error_accounting();
    test_mixed_api_contention();
    puts("rate status tests passed");
    return 0;
}
