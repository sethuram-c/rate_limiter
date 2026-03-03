#include "mutex_rate_limiter.h"

#include <assert.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>

#define THREADS 8
#define CALLS 10000U

typedef struct {
    mutex_rate_limiter_t *mutex;
    ip_rate_limiter_t *lockfree;
    uint32_t ip;
    int use_mutex;
    uint64_t admitted;
    uint64_t denied;
} worker_t;

static void compare_snapshot(mutex_rate_limiter_t *m, ip_rate_limiter_t *lf,
                             uint32_t ip)
{
    bool fm = false, flf = false;
    uint64_t am = 0, alf = 0;
    uint16_t tm = 0, tlf = 0;
    assert(mutex_rate_limiter_snapshot(m, ip, &fm, &am, &tm));
    assert(ip_rate_limiter_snapshot(lf, ip, &flf, &alf, &tlf));
    assert(fm == flf);
    if (fm) {
        assert(am == alf);
        assert(tm == tlf);
    }
}

static void sequential_equivalence(void)
{
    mutex_rate_limiter_t m = {0};
    ip_rate_limiter_t lf = {0};
    bucket_policy_t policy = {7U, 100U};
    struct step { uint32_t ip, tokens; uint64_t now; } steps[] = {
        {1U, 2U, 1000U}, {2U, 5U, 1000U}, {1U, 4U, 1000U},
        {1U, 2U, 1000U}, {2U, 3U, 1050U}, {1U, 1U, 1099U},
        {1U, 1U, 1100U}, {2U, 2U, 1100U}, {1U, 2U, 1101U},
        {1U, 2U, 1300U}, {3U, 7U, 1300U}, {3U, 1U, 1300U},
        {4U, 0U, 1300U}, {4U, 8U, 1300U}, {2U, 1U, 900U},
        {2U, 1U, BUCKET_STATE_MAX_TIME_MS + 1U},
        {4U, 1U, 1300U}, {5U, 1U, 1300U}
    };
    size_t i;
    assert(mutex_rate_limiter_init(&m, 4U, &policy, 1000U));
    assert(ip_rate_limiter_init(&lf, 4U, &policy, 1000U));
    for (i = 0U; i < sizeof(steps)/sizeof(steps[0]); ++i) {
        ip_rate_decision_t mr = mutex_rate_limiter_check(
            &m, steps[i].ip, steps[i].tokens, steps[i].now);
        ip_rate_decision_t lr = ip_rate_limiter_check(
            &lf, steps[i].ip, steps[i].tokens, steps[i].now);
        assert(mr == lr);
        compare_snapshot(&m, &lf, steps[i].ip);
    }
    assert(mutex_rate_limiter_check(&m, 5U, 1U, 1300U) == IP_RATE_TABLE_FULL);
    compare_snapshot(&m, &lf, 5U);
    assert(mutex_rate_limiter_check(&m, 1U, 1U, 1300U) != IP_RATE_TABLE_FULL);
    mutex_rate_limiter_destroy(&m);
    ip_rate_limiter_destroy(&lf);
    mutex_rate_limiter_destroy(&m);
}

static void invalid_inputs(void)
{
    mutex_rate_limiter_t m = {0};
    bucket_policy_t bad = {0U, 100U};
    bucket_policy_t ok = {4U, 100U};
    assert(!mutex_rate_limiter_init(NULL, 4U, &ok, 0U));
    assert(!mutex_rate_limiter_init(&m, 3U, &ok, 0U));
    assert(!mutex_rate_limiter_init(&m, 4U, &bad, 0U));
    assert(!mutex_rate_limiter_init(&m, 4U, &ok,
                                   BUCKET_STATE_MAX_TIME_MS + 1U));
    assert(mutex_rate_limiter_check(&m, 1U, 1U, 0U) == IP_RATE_INVALID);
    assert(mutex_rate_limiter_init(&m, 4U, &ok, 0U));
    assert(!mutex_rate_limiter_init(&m, 4U, &ok, 0U));
    assert(mutex_rate_limiter_check(&m, 1U, 0U, 0U) == IP_RATE_INVALID);
    assert(mutex_rate_limiter_check(&m, 1U, 1U,
                                    BUCKET_STATE_MAX_TIME_MS + 1U) == IP_RATE_INVALID);
    assert(ip_atomic_table_count_quiescent(&m.index) == 0U);
    mutex_rate_limiter_destroy(&m);
}

static void *work(void *ctx)
{
    worker_t *w = ctx;
    uint32_t j;
    for (j = 0U; j < CALLS; ++j) {
        ip_rate_decision_t d = w->use_mutex ?
            mutex_rate_limiter_check(w->mutex, w->ip, 1U, 1000U) :
            ip_rate_limiter_check(w->lockfree, w->ip, 1U, 1000U);
        assert(d == IP_RATE_ALLOWED || d == IP_RATE_DENIED);
        if (d == IP_RATE_ALLOWED) ++w->admitted;
        else ++w->denied;
    }
    return NULL;
}

static void concurrent_equivalence(void)
{
    mutex_rate_limiter_t m = {0};
    ip_rate_limiter_t lf = {0};
    bucket_policy_t p = {37U, 100U};
    int implementation;
    assert(mutex_rate_limiter_init(&m, 8U, &p, 1000U));
    assert(ip_rate_limiter_init(&lf, 8U, &p, 1000U));
    for (implementation = 0; implementation < 2; ++implementation) {
        worker_t w[THREADS] = {{0}};
        pthread_t t[THREADS];
        uint64_t admitted = 0U, denied = 0U;
        size_t i;
        for (i = 0U; i < THREADS; ++i) {
            w[i].mutex = &m;
            w[i].lockfree = &lf;
            w[i].use_mutex = implementation == 0;
            w[i].ip = (uint32_t)(i % 4U) + 1U;
            assert(pthread_create(&t[i], NULL, work, &w[i]) == 0);
        }
        for (i = 0U; i < THREADS; ++i) {
            assert(pthread_join(t[i], NULL) == 0);
            admitted += w[i].admitted;
            denied += w[i].denied;
        }
        assert(admitted == 148U);
        assert(denied == THREADS * CALLS - 148U);
        printf("%s: %u requests, 148 allowed, %llu denied\n",
               implementation == 0 ? "mutex" : "lockfree",
               THREADS * CALLS, (unsigned long long)denied);
    }
    { size_t i; for (i = 1U; i <= 4U; ++i) compare_snapshot(&m, &lf, (uint32_t)i); }
    mutex_rate_limiter_destroy(&m);
    ip_rate_limiter_destroy(&lf);
}

int main(void)
{
    invalid_inputs();
    sequential_equivalence();
    concurrent_equivalence();
    puts("mutex reference tests passed");
    return 0;
}
