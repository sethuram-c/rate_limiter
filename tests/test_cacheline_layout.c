#include "ip_rate_limiter.h"

#include <assert.h>
#include <inttypes.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>

#define THREADS 8U
#define ROUNDS 10000U
#define START_MS UINT64_C(20270105)
#define FIRST_IP UINT32_C(0xc0000200)

typedef struct {
    ip_rate_limiter_t *limiter;
    uint32_t ip;
    uint64_t admitted;
} worker_t;

static void *run_requests(void *arg)
{
    worker_t *w = arg;
    uint32_t i;
    for (i = 0U; i < ROUNDS; ++i) {
        ip_rate_decision_t result = ip_rate_limiter_check(
            w->limiter, w->ip, 1U, START_MS);
        assert(result == IP_RATE_ALLOWED || result == IP_RATE_DENIED);
        if (result == IP_RATE_ALLOWED) {
            ++w->admitted;
        }
    }
    return NULL;
}

int main(void)
{
    ip_rate_limiter_t limiter = {0};
    const bucket_policy_t policy = {41U, UINT64_C(1000)};
    pthread_t threads[THREADS];
    worker_t workers[THREADS] = {{0}};
    uint32_t i;
    size_t stride = sizeof(lf_bucket_t);

#ifdef RL_CACHELINE_PADDED
    assert(stride == 64U);
    assert(_Alignof(lf_bucket_t) >= 64U);
#else
    assert(stride < 64U);
#endif
    assert(ip_rate_limiter_init(&limiter, 16U, &policy, START_MS));
#ifdef RL_CACHELINE_PADDED
    assert(((uintptr_t)limiter.buckets % 64U) == 0U);
    for (i = 0U; i < 16U; ++i) {
        assert(((uintptr_t)&limiter.buckets[i].state % 64U) == 0U);
    }
#endif
    for (i = 0U; i < THREADS; ++i) {
        workers[i].limiter = &limiter;
        workers[i].ip = FIRST_IP + i;
        assert(pthread_create(&threads[i], NULL, run_requests, &workers[i]) == 0);
    }
    for (i = 0U; i < THREADS; ++i) {
        bool found = false;
        uint64_t anchor = 0U;
        uint16_t tokens = UINT16_MAX;
        assert(pthread_join(threads[i], NULL) == 0);
        assert(workers[i].admitted == 41U);
        assert(ip_rate_limiter_snapshot(&limiter, workers[i].ip,
                                        &found, &anchor, &tokens));
        assert(found && anchor == START_MS && tokens == 0U);
    }
    assert(ip_atomic_table_count_quiescent(&limiter.index) == THREADS);
    ip_rate_limiter_destroy(&limiter);
    printf("cacheline layout test passed: stride=%zu alignment=%zu, "
           "%u requests, %u admissions\n", stride, _Alignof(lf_bucket_t),
           THREADS * ROUNDS, THREADS * 41U);
    return 0;
}
