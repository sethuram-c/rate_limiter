#include "mutex_rate_limiter.h"

#include <errno.h>
#include <inttypes.h>
#include <pthread.h>
#include <sched.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define MAX_THREADS 16U
#define MAX_REQUESTS UINT64_C(200000)
#define FIXED_TIME_MS UINT64_C(1000000)
#define BUCKET_CAPACITY UINT64_C(30000)

typedef struct {
    _Atomic unsigned ready;
    _Atomic bool go;
    bool cancel; /* written before release-store(go), never mutated afterward */
} gate_t;

typedef struct {
    gate_t *gate;
    ip_rate_limiter_t *lockfree;
    mutex_rate_limiter_t *mutex;
    bool mutex_mode;
    uint64_t requests;
    uint32_t ip;
    uint64_t allowed;
    uint64_t denied;
    uint64_t errors;
    uint64_t *samples;
    uint64_t sampled;
} worker_t;

static bool parse_uint(const char *s, uint64_t *value)
{
    char *end = NULL;
    unsigned long long parsed;
    if (s == NULL || *s == '\0' || *s == '-') return false;
    errno = 0;
    parsed = strtoull(s, &end, 10);
    if (errno == ERANGE || end == s || *end != '\0') return false;
    *value = (uint64_t)parsed;
    return true;
}

static bool monotonic_ns(uint64_t *out)
{
    struct timespec t;
    if (clock_gettime(CLOCK_MONOTONIC, &t) != 0 || t.tv_sec < 0 ||
        (uint64_t)t.tv_sec > (UINT64_MAX - (uint64_t)t.tv_nsec) / UINT64_C(1000000000)) {
        return false;
    }
    *out = (uint64_t)t.tv_sec * UINT64_C(1000000000) + (uint64_t)t.tv_nsec;
    return true;
}

static ip_rate_decision_t check(worker_t *w)
{
    if (w->mutex_mode) {
        return mutex_rate_limiter_check(w->mutex, w->ip, 1U, FIXED_TIME_MS);
    }
    return ip_rate_limiter_check(w->lockfree, w->ip, 1U, FIXED_TIME_MS);
}

static void *worker_main(void *arg)
{
    worker_t *w = arg;
    uint64_t i;
    atomic_fetch_add_explicit(&w->gate->ready, 1U, memory_order_release);
    while (!atomic_load_explicit(&w->gate->go, memory_order_acquire)) {
        sched_yield(); /* benchmark start gate only, not the request hot path */
    }
    if (w->gate->cancel) return NULL;
    for (i = 0U; i < w->requests; ++i) {
        uint64_t before, after;
        ip_rate_decision_t result;
        if (!monotonic_ns(&before)) { ++w->errors; continue; }
        result = check(w);
        if (!monotonic_ns(&after) || after < before) { ++w->errors; continue; }
        w->samples[w->sampled++] = after - before;
        if (result == IP_RATE_ALLOWED) ++w->allowed;
        else if (result == IP_RATE_DENIED) ++w->denied;
        else ++w->errors;
    }
    return NULL;
}

static int compare_ns(const void *a, const void *b)
{
    uint64_t x = *(const uint64_t *)a, y = *(const uint64_t *)b;
    return (x > y) - (x < y);
}

/* Nearest-rank percentile: sorted[ceil(p*N)-1]; integer ceil for 50/95/99. */
static uint64_t percentile(const uint64_t *sorted, size_t n, size_t percent)
{
    size_t rank = (n / 100U) * percent + ((n % 100U) * percent + 99U) / 100U;
    if (rank == 0U) rank = 1U;
    return sorted[rank - 1U];
}

int main(int argc, char **argv)
{
    uint64_t requested_threads, requests;
    bool mutex_mode, shared;
    bucket_policy_t policy = { (uint16_t)BUCKET_CAPACITY, 100U };
    mutex_rate_limiter_t mutex = {0};
    ip_rate_limiter_t lockfree = {0};
    gate_t gate = {0};
    pthread_t threads[MAX_THREADS];
    worker_t workers[MAX_THREADS] = {{0}};
    uint64_t started = 0U, stopped = 0U;
    uint64_t allowed = 0U, denied = 0U, errors = 0U;
    uint64_t expected = 0U;
    size_t launched = 0U;
    size_t i;
    bool ok;
    uint64_t *all_samples = NULL;
    size_t count = 0U;

    if (argc != 5 || !parse_uint(argv[2], &requested_threads) ||
        !parse_uint(argv[3], &requests) ||
        (strcmp(argv[1], "mutex") != 0 && strcmp(argv[1], "lockfree") != 0) ||
        (strcmp(argv[4], "shared") != 0 && strcmp(argv[4], "sharded") != 0) ||
        requested_threads < 1U || requested_threads > MAX_THREADS ||
        requests < 1U || requests > MAX_REQUESTS) {
        fprintf(stderr, "usage: %s mutex|lockfree THREADS(1..16) REQUESTS(1..10000000) shared|sharded\n", argv[0]);
        return 2;
    }
    all_samples = calloc((size_t)requests, sizeof(*all_samples));
    if (all_samples == NULL) { fputs("sample allocation failed\n", stderr); return 1; }
    mutex_mode = strcmp(argv[1], "mutex") == 0;
    shared = strcmp(argv[4], "shared") == 0;
    if (mutex_mode) {
        if (!mutex_rate_limiter_init(&mutex, 64U, &policy, FIXED_TIME_MS)) {
            fputs("mutex initialization failed\n", stderr);
            return 1;
        }
    } else if (!ip_rate_limiter_init(&lockfree, 64U, &policy, FIXED_TIME_MS)) {
        fputs("lock-free initialization failed (64-bit atomics required)\n", stderr);
        return 1;
    }
    /* Identical prepopulation prevents first-insert work inside timing. */
    for (i = 0; i < (shared ? 1U : (size_t)requested_threads); ++i) {
        ip_rate_decision_t r = mutex_mode ?
            mutex_rate_limiter_check(&mutex, (uint32_t)i + 1U, 1U, FIXED_TIME_MS) :
            ip_rate_limiter_check(&lockfree, (uint32_t)i + 1U, 1U, FIXED_TIME_MS);
        if (r != IP_RATE_ALLOWED) {
            fputs("prepopulation failure\n", stderr);
            goto failed;
        }
    }
    atomic_init(&gate.ready, 0U);
    atomic_init(&gate.go, false);
    gate.cancel = false;
    for (i = 0; i < (size_t)requested_threads; ++i) {
        uint64_t share = requests / requested_threads +
                         (i < requests % requested_threads ? 1U : 0U);
        workers[i].samples = all_samples + count;
        count += (size_t)share;
        workers[i].gate = &gate;
        workers[i].mutex = &mutex;
        workers[i].lockfree = &lockfree;
        workers[i].mutex_mode = mutex_mode;
        workers[i].requests = share;
        workers[i].ip = shared ? 1U : (uint32_t)i + 1U;
        if (pthread_create(&threads[i], NULL, worker_main, &workers[i]) != 0) {
            fputs("pthread_create failed\n", stderr);
            gate.cancel = true;
            atomic_store_explicit(&gate.go, true, memory_order_release);
            while (launched > 0U) { --launched; (void)pthread_join(threads[launched], NULL); }
            goto failed;
        }
        ++launched;
        if (!shared) expected += share < BUCKET_CAPACITY - 1U ? share : BUCKET_CAPACITY - 1U;
    }
    if (shared) expected = requests < BUCKET_CAPACITY - 1U ? requests : BUCKET_CAPACITY - 1U;
    while (atomic_load_explicit(&gate.ready, memory_order_acquire) < requested_threads) {
        sched_yield();
    }
    if (!monotonic_ns(&started)) {
        fputs("clock_gettime failed\n", stderr);
        gate.cancel = true;
        atomic_store_explicit(&gate.go, true, memory_order_release);
        for (i = 0U; i < launched; ++i) (void)pthread_join(threads[i], NULL);
        goto failed;
    }
    atomic_store_explicit(&gate.go, true, memory_order_release);
    for (i = 0; i < launched; ++i) {
        if (pthread_join(threads[i], NULL) != 0) {
            fputs("pthread_join failed\n", stderr);
            goto failed;
        }
    }
    if (!monotonic_ns(&stopped) || stopped <= started) {
        fputs("invalid monotonic duration\n", stderr);
        goto failed;
    }
    for (i = 0; i < launched; ++i) {
        allowed += workers[i].allowed;
        denied += workers[i].denied;
        errors += workers[i].errors;
    }
    count = 0U;
    for (i = 0; i < launched; ++i) count += (size_t)workers[i].sampled;
    qsort(all_samples, count, sizeof(*all_samples), compare_ns);
    ok = (count == requests && allowed + denied + errors == requests &&
          allowed == expected && denied == requests - expected && errors == 0U);
    printf("impl=%s scenario=%s threads=%" PRIu64 " requests=%" PRIu64
           " allowed=%" PRIu64 " denied=%" PRIu64 " errors=%" PRIu64
           " elapsed_ns=%" PRIu64 " ops_per_sec=%.0f valid=%s\n",
           argv[1], argv[4], requested_threads, requests, allowed,
           denied, errors, stopped - started,
           (double)requests * 1000000000.0 / (double)(stopped - started),
           ok ? "yes" : "no");
    if (count != 0U) {
        printf("latency_samples=%zu p50_ns=%" PRIu64 " p95_ns=%" PRIu64
               " p99_ns=%" PRIu64 " max_ns=%" PRIu64 "\n", count,
               percentile(all_samples, count, 50U), percentile(all_samples, count, 95U),
               percentile(all_samples, count, 99U), all_samples[count - 1U]);
    }
    free(all_samples);
    mutex_rate_limiter_destroy(&mutex);
    ip_rate_limiter_destroy(&lockfree);
    return ok ? 0 : 1;
failed:
    free(all_samples);
    mutex_rate_limiter_destroy(&mutex);
    ip_rate_limiter_destroy(&lockfree);
    return 1;
}
