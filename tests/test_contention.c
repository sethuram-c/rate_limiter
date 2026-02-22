#include "lockfree_bucket.h"
#include "linearizability_check.h"

#include <assert.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#define STRESS_WORKERS 8
#define STRESS_ROUNDS 10
#define STRESS_REQUESTS_PER_WORKER 12000
#define TRACE_WORKERS 8
#define TRACE_ROUNDS 100

typedef struct {
    lf_bucket_t *bucket;
    pthread_barrier_t *barrier;
    uint64_t attempts;
    uint64_t admitted;
    uint64_t denied;
    uint64_t cas_attempts;
    uint64_t retried_calls;
    uint64_t maximum_cas_attempts;
} stress_worker_t;

static void *stress_work(void *arg)
{
    stress_worker_t *w = arg;
    const int rc = pthread_barrier_wait(w->barrier);
    assert(rc == 0 || rc == PTHREAD_BARRIER_SERIAL_THREAD);
    for (unsigned i = 0; i < STRESS_REQUESTS_PER_WORKER; ++i) {
        uint64_t attempts = 0U;
        bool allowed = false;
        assert(lf_bucket_try_consume_profiled(w->bucket, 1U, 1000U,
                                               &allowed, &attempts));
        assert(attempts >= 1U);
        ++w->attempts;
        w->admitted += allowed ? 1U : 0U;
        w->denied += allowed ? 0U : 1U;
        w->cas_attempts += attempts;
        w->retried_calls += attempts > 1U ? 1U : 0U;
        if (attempts > w->maximum_cas_attempts) {
            w->maximum_cas_attempts = attempts;
        }
    }
    return NULL;
}

static void test_stress_and_metrics(void)
{
    const bucket_policy_t policy = {.capacity = 5000U,
                                    .refill_interval_ms = 1000U};
    uint64_t all_calls = 0U;
    uint64_t admitted = 0U;
    uint64_t denied = 0U;
    uint64_t cas_attempts = 0U;
    uint64_t retried = 0U;
    uint64_t high_water = 0U;

    for (int round = 0; round < STRESS_ROUNDS; ++round) {
        lf_bucket_t bucket;
        pthread_barrier_t barrier;
        pthread_t threads[STRESS_WORKERS];
        stress_worker_t workers[STRESS_WORKERS] = {0};
        uint64_t anchor = 0U;
        uint16_t tokens = 0U;
        assert(lf_bucket_init(&bucket, &policy, 1000U));
        assert(pthread_barrier_init(&barrier, NULL, STRESS_WORKERS) == 0);
        for (int i = 0; i < STRESS_WORKERS; ++i) {
            workers[i].bucket = &bucket;
            workers[i].barrier = &barrier;
            assert(pthread_create(&threads[i], NULL, stress_work,
                                  &workers[i]) == 0);
        }
        for (int i = 0; i < STRESS_WORKERS; ++i) {
            assert(pthread_join(threads[i], NULL) == 0);
            all_calls += workers[i].attempts;
            admitted += workers[i].admitted;
            denied += workers[i].denied;
            cas_attempts += workers[i].cas_attempts;
            retried += workers[i].retried_calls;
            if (workers[i].maximum_cas_attempts > high_water) {
                high_water = workers[i].maximum_cas_attempts;
            }
        }
        assert(pthread_barrier_destroy(&barrier) == 0);
        assert(lf_bucket_snapshot(&bucket, &anchor, &tokens));
        assert(anchor == 1000U && tokens == 0U);
    }
    assert(all_calls == (uint64_t)STRESS_ROUNDS * STRESS_WORKERS *
                            STRESS_REQUESTS_PER_WORKER);
    assert(admitted == (uint64_t)STRESS_ROUNDS * policy.capacity);
    assert(denied == all_calls - admitted);
    assert(cas_attempts >= all_calls);
    assert(retried <= all_calls);
    assert(high_water >= 1U);
    printf("stress: calls=%llu allowed=%llu denied=%llu "
           "CAS_attempts=%llu retries=%llu retried_calls=%llu max_attempts=%llu\n",
           (unsigned long long)all_calls,
           (unsigned long long)admitted,
           (unsigned long long)denied,
           (unsigned long long)cas_attempts,
           (unsigned long long)(cas_attempts - all_calls),
           (unsigned long long)retried,
           (unsigned long long)high_water);
}

typedef struct {
    lf_bucket_t *bucket;
    pthread_barrier_t *barrier;
    _Atomic uint64_t *event_clock;
    linear_event_t *event;
    uint32_t amount;
    uint64_t now_ms;
} trace_worker_t;

static void *trace_work(void *arg)
{
    trace_worker_t *w = arg;
    bool allowed = false;
    const int rc = pthread_barrier_wait(w->barrier);
    assert(rc == 0 || rc == PTHREAD_BARRIER_SERIAL_THREAD);
    w->event->invoked = atomic_fetch_add_explicit(
        w->event_clock, 1U, memory_order_seq_cst);
    assert(lf_bucket_try_consume(w->bucket, w->amount, w->now_ms,
                                 &allowed));
    w->event->returned = atomic_fetch_add_explicit(
        w->event_clock, 1U, memory_order_seq_cst);
    w->event->amount = w->amount;
    w->event->now_ms = w->now_ms;
    w->event->allowed = allowed;
    return NULL;
}

static void test_linearizability_traces(void)
{
    const bucket_policy_t policy = {.capacity = 7,
                                    .refill_interval_ms = 1000};
    uint32_t seeds[TRACE_WORKERS] = {1, 2, 1, 2, 1, 2, 1, 2};
    uint64_t histories = 0U;

    for (unsigned round = 0; round < TRACE_ROUNDS; ++round) {
        lf_bucket_t bucket;
        pthread_barrier_t barrier;
        pthread_t threads[TRACE_WORKERS];
        trace_worker_t workers[TRACE_WORKERS] = {0};
        linear_event_t events[TRACE_WORKERS] = {0};
        _Atomic uint64_t event_clock;
        uint64_t initial_state = 0U;
        uint64_t final_state;
        uint32_t amounts[TRACE_WORKERS];
        assert(bucket_state_initial(&policy, 1000, &initial_state));
        assert(lf_bucket_init(&bucket, &policy, 1000));
        assert(pthread_barrier_init(&barrier, NULL, TRACE_WORKERS) == 0);
        atomic_init(&event_clock, 0U);
        for (int i = 0; i < TRACE_WORKERS; ++i) {
            /* Mix one- and two-token requests; alternate two time samples. */
            amounts[i] = seeds[(i + (int)round) % TRACE_WORKERS];
            workers[i].bucket = &bucket;
            workers[i].barrier = &barrier;
            workers[i].event_clock = &event_clock;
            workers[i].event = &events[i];
            workers[i].amount = amounts[i];
            workers[i].now_ms = (i % 2 == 0) ? 1000U : 2000U;
            assert(pthread_create(&threads[i], NULL, trace_work,
                                  &workers[i]) == 0);
        }
        for (int i = 0; i < TRACE_WORKERS; ++i) {
            assert(pthread_join(threads[i], NULL) == 0);
        }
        assert(pthread_barrier_destroy(&barrier) == 0);
        final_state = atomic_load_explicit(&bucket.state,
                                           memory_order_relaxed);
        {
            uint64_t final_anchor = 0U;
            bucket_state_unpack(final_state, &final_anchor, NULL);
            assert(final_anchor == 2000U); /* Mixed-time trace refilled. */
        }
        assert(linear_history_valid(events, TRACE_WORKERS, &policy,
                                    initial_state, final_state));
        ++histories;
    }
    printf("linearizability: %llu real concurrent histories accepted\n",
           (unsigned long long)histories);
}

static void test_oracle_rejects_corruption(void)
{
    const bucket_policy_t policy = {.capacity = 1,
                                    .refill_interval_ms = 1000};
    uint64_t initial_state;
    uint64_t final_state;
    const linear_event_t impossible[] = {
        {.invoked = 0U, .returned = 2U, .now_ms = 100U,
         .amount = 1U, .allowed = true},
        {.invoked = 1U, .returned = 3U, .now_ms = 100U,
         .amount = 1U, .allowed = true},
    };
    const linear_event_t possible[] = {
        {.invoked = 0U, .returned = 2U, .now_ms = 100U,
         .amount = 1U, .allowed = true},
        {.invoked = 1U, .returned = 3U, .now_ms = 100U,
         .amount = 1U, .allowed = false},
    };
    assert(bucket_state_initial(&policy, 100U, &initial_state));
    assert(bucket_state_pack(100U, 0U, &final_state));
    assert(!linear_history_valid(impossible, 2U, &policy,
                                 initial_state, final_state));
    assert(linear_history_valid(possible, 2U, &policy,
                                initial_state, final_state));
    /* A wrong final state must not be accepted even with valid decisions. */
    assert(!linear_history_valid(possible, 2U, &policy,
                                 initial_state, initial_state));
    puts("checker negative controls passed");
}

static void test_profiled_contract(void)
{
    lf_bucket_t bucket;
    const bucket_policy_t policy = {.capacity = 2U,
                                    .refill_interval_ms = 100U};
    bool allowed = false;
    uint64_t attempts = 999U;
    assert(lf_bucket_init(&bucket, &policy, 100U));
    assert(!lf_bucket_try_consume_profiled(&bucket, 0U, 100U,
                                            &allowed, &attempts));
    assert(attempts == 999U && !allowed);
    assert(!lf_bucket_try_consume_profiled(&bucket, 1U, 100U,
                                            &allowed, NULL));
    assert(lf_bucket_try_consume_profiled(&bucket, 1U, 100U,
                                           &allowed, &attempts));
    assert(allowed && attempts >= 1U);
}

int main(void)
{
    test_profiled_contract();
    test_oracle_rejects_corruption();
    test_stress_and_metrics();
    test_linearizability_traces();
    puts("phase 8 contention and bounded linearizability tests passed");
    return 0;
}
