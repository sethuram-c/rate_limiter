#include "ip_rate_limiter.h"

#include <assert.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#define WORKERS 12U
#define SAME_IP_ROUNDS 7500U
#define MANY_IP_ROUNDS 6400U
#define MANY_IPS 64U
#define MANY_IP_CAPACITY 37U

typedef struct {
    ip_rate_limiter_t *limiter;
    pthread_barrier_t *barrier;
    bool many_ips;
    uint64_t allowed;
    uint64_t denied;
    uint64_t failures;
} worker_arg_t;

static void *worker(void *arg_void)
{
    worker_arg_t *arg = arg_void;
    size_t i;
    size_t rounds = arg->many_ips ? MANY_IP_ROUNDS : SAME_IP_ROUNDS;
    int barrier_status = pthread_barrier_wait(arg->barrier);
    assert(barrier_status == 0 || barrier_status == PTHREAD_BARRIER_SERIAL_THREAD);
    for (i = 0U; i < rounds; ++i) {
        uint32_t ip = arg->many_ips
            ? UINT32_C(0xcb007100) + (uint32_t)(i % MANY_IPS)
            : UINT32_C(0xc0000201);
        ip_rate_decision_t result = ip_rate_limiter_check(
            arg->limiter, ip, 1U, 1000U);
        if (result == IP_RATE_ALLOWED) {
            ++arg->allowed;
        } else if (result == IP_RATE_DENIED) {
            ++arg->denied;
        } else {
            ++arg->failures;
        }
    }
    return NULL;
}

static void run_concurrent_case(bool many_ips)
{
    const bucket_policy_t policy = {
        many_ips ? MANY_IP_CAPACITY : 173U,
        1000U
    };
    const size_t slots = many_ips ? 128U : 8U;
    const uint64_t total_requests = (uint64_t)WORKERS *
        (many_ips ? MANY_IP_ROUNDS : SAME_IP_ROUNDS);
    const uint64_t expected_allowed = many_ips
        ? (uint64_t)MANY_IPS * MANY_IP_CAPACITY : 173U;
    ip_rate_limiter_t limiter = {0};
    pthread_barrier_t barrier;
    pthread_t threads[WORKERS];
    worker_arg_t args[WORKERS] = {{0}};
    size_t i;
    uint64_t allowed = 0U, denied = 0U, failures = 0U;

    assert(ip_rate_limiter_init(&limiter, slots, &policy, 1000U));
    assert(pthread_barrier_init(&barrier, NULL, WORKERS) == 0);
    for (i = 0U; i < WORKERS; ++i) {
        args[i].limiter = &limiter;
        args[i].barrier = &barrier;
        args[i].many_ips = many_ips;
        assert(pthread_create(&threads[i], NULL, worker, &args[i]) == 0);
    }
    for (i = 0U; i < WORKERS; ++i) {
        assert(pthread_join(threads[i], NULL) == 0);
        allowed += args[i].allowed;
        denied += args[i].denied;
        failures += args[i].failures;
    }
    assert(pthread_barrier_destroy(&barrier) == 0);
    assert(failures == 0U);
    assert(allowed == expected_allowed);
    assert(denied == total_requests - expected_allowed);
    assert(ip_atomic_table_count_quiescent(&limiter.index) ==
           (many_ips ? MANY_IPS : 1U));
    for (i = 0U; i < (many_ips ? MANY_IPS : 1U); ++i) {
        bool found = false;
        uint16_t tokens = UINT16_MAX;
        uint64_t anchor = 0U;
        uint32_t ip = many_ips ? UINT32_C(0xcb007100) + (uint32_t)i
                               : UINT32_C(0xc0000201);
        assert(ip_rate_limiter_snapshot(&limiter, ip, &found,
                                        &anchor, &tokens));
        assert(found);
        assert(tokens == 0U);
        assert(anchor == 1000U);
    }
    printf("per-IP contention: %llu requests, %llu allowed, %llu denied, %zu IPs\n",
           (unsigned long long)total_requests,
           (unsigned long long)allowed,
           (unsigned long long)denied,
           many_ips ? (size_t)MANY_IPS : (size_t)1U);
    ip_rate_limiter_destroy(&limiter);
}

static void test_initialization_and_validation(void)
{
    const bucket_policy_t valid = {2U, 100U};
    const bucket_policy_t invalid = {0U, 100U};
    ip_rate_limiter_t limiter = {0};
    bool found = true;
    uint16_t tokens = 999U;
    uint64_t anchor = 999U;

    assert(!ip_rate_limiter_init(NULL, 8U, &valid, 1000U));
    assert(!ip_rate_limiter_init(&limiter, 3U, &valid, 1000U));
    assert(!ip_rate_limiter_init(&limiter, 8U, &invalid, 1000U));
    assert(!ip_rate_limiter_init(&limiter, 8U, &valid,
                                 BUCKET_STATE_MAX_TIME_MS + UINT64_C(1)));
    assert(ip_rate_limiter_init(&limiter, 8U, &valid, 1000U));
    assert(!ip_rate_limiter_init(&limiter, 8U, &valid, 1000U));
    assert(ip_rate_limiter_check(NULL, 123U, 1U, 1000U) == IP_RATE_INVALID);
    assert(ip_rate_limiter_check(&limiter, 123U, 0U, 1000U) == IP_RATE_INVALID);
    assert(ip_rate_limiter_check(&limiter, 123U, 1U,
           BUCKET_STATE_MAX_TIME_MS + UINT64_C(1)) == IP_RATE_INVALID);
    assert(ip_atomic_table_count_quiescent(&limiter.index) == 0U);
    assert(ip_rate_limiter_snapshot(&limiter, 123U, &found,
                                    &anchor, &tokens));
    assert(!found && anchor == 999U && tokens == 999U);
    assert(!ip_rate_limiter_snapshot(&limiter, 123U, NULL,
                                     &anchor, &tokens));
    ip_rate_limiter_destroy(&limiter);
    assert(limiter.buckets == NULL && limiter.index.slots == NULL);
    assert(ip_rate_limiter_check(&limiter, 123U, 1U, 1000U) == IP_RATE_INVALID);
    ip_rate_limiter_destroy(&limiter);
}

static void test_independence_refill_and_full_table(void)
{
    const bucket_policy_t policy = {2U, 100U};
    ip_rate_limiter_t limiter = {0};
    bool found;
    uint16_t tokens;
    uint64_t anchor;

    assert(ip_rate_limiter_init(&limiter, 4U, &policy, 1000U));
    assert(ip_rate_limiter_check(&limiter, 0U, 2U, 1000U) == IP_RATE_ALLOWED);
    assert(ip_rate_limiter_check(&limiter, 0U, 1U, 1000U) == IP_RATE_DENIED);
    assert(ip_rate_limiter_check(&limiter, UINT32_MAX, 1U, 1000U) == IP_RATE_ALLOWED);
    assert(ip_rate_limiter_check(&limiter, UINT32_MAX, 1U, 1000U) == IP_RATE_ALLOWED);
    assert(ip_rate_limiter_check(&limiter, UINT32_MAX, 1U, 1000U) == IP_RATE_DENIED);
    assert(ip_rate_limiter_check(&limiter, 0U, 1U, 1099U) == IP_RATE_DENIED);
    assert(ip_rate_limiter_check(&limiter, 0U, 1U, 1100U) == IP_RATE_ALLOWED);
    assert(ip_rate_limiter_check(&limiter, 17U, 1U, 1100U) == IP_RATE_ALLOWED);
    assert(ip_rate_limiter_check(&limiter, 18U, 1U, 1100U) == IP_RATE_ALLOWED);
    assert(ip_rate_limiter_check(&limiter, 19U, 1U, 1100U) == IP_RATE_TABLE_FULL);
    /* A full table still serves previously published keys. */
    assert(ip_rate_limiter_check(&limiter, 0U, 1U, 1200U) == IP_RATE_ALLOWED);
    assert(ip_rate_limiter_snapshot(&limiter, 0U, &found, &anchor, &tokens));
    assert(found && anchor == 1200U && tokens == 0U);
    assert(ip_atomic_table_count_quiescent(&limiter.index) == 4U);
    ip_rate_limiter_destroy(&limiter);
}

int main(void)
{
    test_initialization_and_validation();
    test_independence_refill_and_full_table();
    run_concurrent_case(false);
    run_concurrent_case(true);
    puts("per-IP rate limiter tests passed");
    return 0;
}
