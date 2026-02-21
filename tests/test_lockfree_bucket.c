#include "lockfree_bucket.h"

#include <assert.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#define WORKERS 8
#define ATTEMPTS 20000

typedef struct {
    lf_bucket_t *bucket;
    pthread_barrier_t *start;
    uint64_t now_ms;
    uint32_t amount;
    unsigned successes;
    unsigned denials;
} worker_args_t;

static void *consume_worker(void *raw)
{
    worker_args_t *w = raw;
    int rc = pthread_barrier_wait(w->start);
    assert(rc == 0 || rc == PTHREAD_BARRIER_SERIAL_THREAD);

    for (int i = 0; i < ATTEMPTS; ++i) {
        bool allowed = false;
        assert(lf_bucket_try_consume(w->bucket, w->amount, w->now_ms,
                                     &allowed));
        if (allowed) {
            ++w->successes;
        } else {
            ++w->denials;
        }
    }
    return NULL;
}

static void run_workers(lf_bucket_t *bucket, uint64_t now_ms,
                        uint32_t amount, unsigned expected_successes)
{
    pthread_t threads[WORKERS];
    worker_args_t args[WORKERS] = {0};
    pthread_barrier_t start;
    unsigned successes = 0U;
    unsigned denials = 0U;

    assert(pthread_barrier_init(&start, NULL, WORKERS) == 0);
    for (int i = 0; i < WORKERS; ++i) {
        args[i].bucket = bucket;
        args[i].start = &start;
        args[i].now_ms = now_ms;
        args[i].amount = amount;
        assert(pthread_create(&threads[i], NULL, consume_worker,
                              &args[i]) == 0);
    }
    for (int i = 0; i < WORKERS; ++i) {
        assert(pthread_join(threads[i], NULL) == 0);
        successes += args[i].successes;
        denials += args[i].denials;
    }
    assert(pthread_barrier_destroy(&start) == 0);
    assert(successes == expected_successes);
    assert(successes + denials == WORKERS * ATTEMPTS);
}

static void test_initialization_and_contract(void)
{
    lf_bucket_t bucket;
    bucket_policy_t p = {.capacity = 3, .refill_interval_ms = 100};
    bucket_policy_t bad = {.capacity = 3, .refill_interval_ms = 0};
    uint64_t anchor = 0;
    uint16_t tokens = 0;
    bool allowed = false;

    assert(!lf_bucket_init(NULL, &p, 1000));
    assert(!lf_bucket_init(&bucket, NULL, 1000));
    assert(!lf_bucket_init(&bucket, &bad, 1000));
    assert(!lf_bucket_init(&bucket, &p, BUCKET_STATE_MAX_TIME_MS + 1U));
    assert(lf_bucket_init(&bucket, &p, 1000));
    assert(atomic_is_lock_free(&bucket.state));
    assert(lf_bucket_snapshot(&bucket, &anchor, &tokens));
    assert(anchor == 1000 && tokens == 3);
    assert(!lf_bucket_snapshot(&bucket, NULL, &tokens));
    assert(!lf_bucket_snapshot(NULL, &anchor, &tokens));

    allowed = true;
    assert(!lf_bucket_try_consume(&bucket, 0, 1000, &allowed));
    assert(allowed);
    assert(!lf_bucket_try_consume(&bucket, 1, 1000, NULL));
    assert(!lf_bucket_try_consume(&bucket, 1,
                                  BUCKET_STATE_MAX_TIME_MS + 1U, &allowed));
    assert(!lf_bucket_try_consume(NULL, 1, 1000, &allowed));
    assert(lf_bucket_snapshot(&bucket, &anchor, &tokens));
    assert(anchor == 1000 && tokens == 3);

    assert(lf_bucket_try_consume(&bucket, 2, 1000, &allowed) && allowed);
    assert(lf_bucket_try_consume(&bucket, 2, 1000, &allowed) && !allowed);
    assert(lf_bucket_snapshot(&bucket, &anchor, &tokens) && tokens == 1);
    assert(lf_bucket_try_consume(&bucket, 1, 1100, &allowed) && allowed);
    assert(lf_bucket_snapshot(&bucket, &anchor, &tokens));
    assert(anchor == 1100 && tokens == 1);
    /* A stale time sample cannot rewind a later anchor. */
    assert(lf_bucket_try_consume(&bucket, 1, 1000, &allowed) && allowed);
    assert(lf_bucket_snapshot(&bucket, &anchor, &tokens));
    assert(anchor == 1100 && tokens == 0);
}

static void test_heavy_contention_no_overspending(void)
{
    lf_bucket_t bucket;
    bucket_policy_t p = {.capacity = 5000, .refill_interval_ms = 1000};
    uint64_t anchor;
    uint16_t tokens;

    assert(lf_bucket_init(&bucket, &p, 1000));
    run_workers(&bucket, 1000, 1, 5000);
    assert(lf_bucket_snapshot(&bucket, &anchor, &tokens));
    assert(anchor == 1000 && tokens == 0);
    run_workers(&bucket, 1999, 1, 0);
    assert(lf_bucket_snapshot(&bucket, &anchor, &tokens));
    assert(anchor == 1000 && tokens == 0);
    run_workers(&bucket, 3000, 1, 2);
    assert(lf_bucket_snapshot(&bucket, &anchor, &tokens));
    assert(anchor == 3000 && tokens == 0);
    run_workers(&bucket, 3000, 1, 0);
    puts("lock-free contention: 640000 attempts, exactly 5002 allowed");
}

static void test_multi_token_atomicity(void)
{
    lf_bucket_t bucket;
    bucket_policy_t p = {.capacity = 301, .refill_interval_ms = 1000};
    uint64_t anchor;
    uint16_t tokens;

    assert(lf_bucket_init(&bucket, &p, 12345));
    run_workers(&bucket, 12345, 3, 100);
    assert(lf_bucket_snapshot(&bucket, &anchor, &tokens));
    assert(anchor == 12345 && tokens == 1);
    puts("lock-free batch contention: exactly 100 three-token grants");
}

static void test_limits_and_saturation(void)
{
    lf_bucket_t bucket;
    bucket_policy_t p = {.capacity = UINT16_MAX, .refill_interval_ms = 1};
    bool allowed;
    uint64_t anchor;
    uint16_t tokens;

    assert(lf_bucket_init(&bucket, &p, BUCKET_STATE_MAX_TIME_MS - 2));
    assert(lf_bucket_try_consume(&bucket, UINT16_MAX,
                                  BUCKET_STATE_MAX_TIME_MS - 2, &allowed));
    assert(allowed);
    assert(lf_bucket_try_consume(&bucket, 1,
                                  BUCKET_STATE_MAX_TIME_MS, &allowed));
    assert(allowed);
    assert(lf_bucket_snapshot(&bucket, &anchor, &tokens));
    assert(anchor == BUCKET_STATE_MAX_TIME_MS && tokens == 1);
    assert(lf_bucket_try_consume(&bucket, UINT32_MAX,
                                  BUCKET_STATE_MAX_TIME_MS, &allowed));
    assert(!allowed);
}

int main(void)
{
    test_initialization_and_contract();
    test_heavy_contention_no_overspending();
    test_multi_token_atomicity();
    test_limits_and_saturation();
    puts("lock-free token bucket tests passed");
    return 0;
}
