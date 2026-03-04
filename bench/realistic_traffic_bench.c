#include "mutex_rate_limiter.h"
#include "traffic_workload.h"

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
#define MAX_IPS 1024U
#define MAX_REQUESTS UINT64_C(10000000)
#define IP_BASE UINT32_C(0xc6120000)
#define TOKEN_CAPACITY UINT16_C(128)
#define NOW_MS UINT64_C(1000000)

typedef struct {
    _Atomic unsigned ready;
    _Atomic bool go;
    bool cancel; /* published by release-store to go */
} start_gate_t;

typedef struct {
    start_gate_t *gate;
    traffic_workload_t traffic;
    ip_rate_limiter_t *lf;
    mutex_rate_limiter_t *mtx;
    bool use_mutex;
    bool mixed;
    uint32_t worker_id;
    uint64_t count;
    uint64_t allowed;
    uint64_t denied;
    uint64_t errors;
    uint64_t allowed_cost;
    uint64_t *spent_by_ip; /* per-worker memory, read only after pthread_join */
} worker_t;

static bool parse_integer(const char *text, uint64_t *out)
{
    unsigned long long n;
    char *end;
    if (text == NULL || *text == '\0' || *text == '-') return false;
    errno = 0;
    n = strtoull(text, &end, 10);
    if (errno == ERANGE || end == text || *end != '\0') return false;
    *out = (uint64_t)n;
    return true;
}

static bool now_ns(uint64_t *out)
{
    struct timespec t;
    if (clock_gettime(CLOCK_MONOTONIC, &t) != 0 || t.tv_sec < 0 ||
        t.tv_nsec < 0 || t.tv_nsec >= 1000000000L ||
        (uint64_t)t.tv_sec > (UINT64_MAX - (uint64_t)t.tv_nsec) / UINT64_C(1000000000)) {
        return false;
    }
    *out = (uint64_t)t.tv_sec * UINT64_C(1000000000) + (uint64_t)t.tv_nsec;
    return true;
}

static void *worker_main(void *arg)
{
    worker_t *w = arg;
    uint64_t i;
    atomic_fetch_add_explicit(&w->gate->ready, 1U, memory_order_release);
    while (!atomic_load_explicit(&w->gate->go, memory_order_acquire)) sched_yield();
    if (w->gate->cancel) return NULL;
    for (i = 0; i < w->count; ++i) {
        uint32_t ip, cost;
        ip_rate_decision_t decision;
        if (!traffic_workload_ip(&w->traffic, w->worker_id, i, &ip) ||
            !traffic_workload_cost(&w->traffic, w->worker_id, i, w->mixed, &cost)) {
            ++w->errors;
            continue;
        }
        decision = w->use_mutex ?
            mutex_rate_limiter_check(w->mtx, ip, cost, NOW_MS) :
            ip_rate_limiter_check(w->lf, ip, cost, NOW_MS);
        if (decision == IP_RATE_ALLOWED) {
            ++w->allowed;
            w->allowed_cost += cost;
            w->spent_by_ip[ip - IP_BASE] += cost;
        } else if (decision == IP_RATE_DENIED) {
            ++w->denied;
        } else {
            ++w->errors;
        }
    }
    return NULL;
}

static bool check_balances(ip_rate_limiter_t *lf, mutex_rate_limiter_t *mtx,
                           bool use_mutex, worker_t *workers,
                           uint32_t threads, uint32_t ip_count)
{
    uint32_t ip;
    for (ip = 0U; ip < ip_count; ++ip) {
        uint64_t spent = 0U, anchor;
        uint16_t tokens;
        bool found = false;
        uint32_t worker;
        bool ok;
        for (worker = 0U; worker < threads; ++worker) {
            spent += workers[worker].spent_by_ip[ip];
        }
        ok = use_mutex ?
            mutex_rate_limiter_snapshot(mtx, IP_BASE + ip, &found, &anchor, &tokens) :
            ip_rate_limiter_snapshot(lf, IP_BASE + ip, &found, &anchor, &tokens);
        /* Every bucket was primed with a one-token request, before timing. */
        if (!ok || !found || anchor != NOW_MS ||
            spent > TOKEN_CAPACITY - 1U ||
            (uint64_t)tokens + spent != TOKEN_CAPACITY - 1U) return false;
    }
    return true;
}

int main(int argc, char **argv)
{
    uint64_t arg_threads, total, arg_ips, seed = UINT64_C(20270104);
    uint32_t threads_count, ip_count;
    traffic_pattern_t pattern;
    bool use_mutex, mixed, valid = false, cancelled = false;
    traffic_workload_t workload;
    bucket_policy_t policy = { TOKEN_CAPACITY, 100U };
    ip_rate_limiter_t lf = {0};
    mutex_rate_limiter_t mtx = {0};
    pthread_t threads[MAX_THREADS];
    worker_t workers[MAX_THREADS] = {{0}};
    start_gate_t gate = {0};
    uint64_t start = 0U, end = 0U, allowed = 0U, denied = 0U;
    uint64_t errors = 0U, spent = 0U;
    size_t launched = 0U;
    uint32_t i;

    if ((argc != 7 && argc != 8) ||
        (strcmp(argv[1], "mutex") != 0 && strcmp(argv[1], "lockfree") != 0) ||
        !parse_integer(argv[2], &arg_threads) || !parse_integer(argv[3], &total) ||
        !parse_integer(argv[5], &arg_ips) ||
        (argc == 8 && !parse_integer(argv[7], &seed)) ||
        arg_threads == 0U || arg_threads > MAX_THREADS ||
        total == 0U || total > MAX_REQUESTS ||
        arg_ips == 0U || arg_ips > MAX_IPS ||
        (strcmp(argv[4], "hot") != 0 && strcmp(argv[4], "uniform") != 0 &&
         strcmp(argv[4], "skewed") != 0) ||
        (strcmp(argv[6], "unit") != 0 && strcmp(argv[6], "mixed") != 0)) {
        fprintf(stderr, "usage: %s mutex|lockfree THREADS(1..16) REQUESTS(1..10000000) hot|uniform|skewed IPS(1..1024) unit|mixed [SEED]\n", argv[0]);
        return 2;
    }
    threads_count = (uint32_t)arg_threads;
    ip_count = (uint32_t)arg_ips;
    use_mutex = strcmp(argv[1], "mutex") == 0;
    mixed = strcmp(argv[6], "mixed") == 0;
    pattern = strcmp(argv[4], "hot") == 0 ? TRAFFIC_HOT :
              strcmp(argv[4], "uniform") == 0 ? TRAFFIC_UNIFORM : TRAFFIC_SKEWED;
    workload = (traffic_workload_t){ seed, ip_count, pattern };

    if (use_mutex) {
        if (!mutex_rate_limiter_init(&mtx, (size_t)ip_count * 2U, &policy, NOW_MS)) goto cleanup;
    } else if (!ip_rate_limiter_init(&lf, (size_t)ip_count * 2U, &policy, NOW_MS)) {
        goto cleanup;
    }
    /* Make first-insertion costs symmetrical and exclude them from timing. */
    for (i = 0U; i < ip_count; ++i) {
        ip_rate_decision_t decision = use_mutex ?
            mutex_rate_limiter_check(&mtx, IP_BASE + i, 1U, NOW_MS) :
            ip_rate_limiter_check(&lf, IP_BASE + i, 1U, NOW_MS);
        if (decision != IP_RATE_ALLOWED) goto cleanup;
    }
    atomic_init(&gate.ready, 0U);
    atomic_init(&gate.go, false);
    gate.cancel = false;
    for (i = 0U; i < threads_count; ++i) {
        worker_t *w = &workers[i];
        w->spent_by_ip = calloc(ip_count, sizeof(*w->spent_by_ip));
        if (w->spent_by_ip == NULL) { cancelled = true; break; }
        w->gate = &gate;
        w->traffic = workload;
        w->lf = &lf;
        w->mtx = &mtx;
        w->use_mutex = use_mutex;
        w->mixed = mixed;
        w->worker_id = i;
        w->count = total / threads_count + (i < total % threads_count ? 1U : 0U);
        if (pthread_create(&threads[i], NULL, worker_main, w) != 0) {
            cancelled = true;
            break;
        }
        ++launched;
    }
    if (!cancelled) {
        while (atomic_load_explicit(&gate.ready, memory_order_acquire) < threads_count) {
            sched_yield();
        }
        if (!now_ns(&start)) cancelled = true;
    }
    gate.cancel = cancelled;
    atomic_store_explicit(&gate.go, true, memory_order_release);
    for (i = 0U; i < launched; ++i) {
        if (pthread_join(threads[i], NULL) != 0) cancelled = true;
    }
    if (cancelled || !now_ns(&end) || end <= start) goto cleanup;
    for (i = 0U; i < threads_count; ++i) {
        allowed += workers[i].allowed;
        denied += workers[i].denied;
        errors += workers[i].errors;
        spent += workers[i].allowed_cost;
    }
    valid = (allowed + denied + errors == total && errors == 0U &&
             spent >= allowed && spent <= allowed * 3U &&
             check_balances(&lf, &mtx, use_mutex, workers, threads_count, ip_count));
    printf("impl=%s pattern=%s costs=%s threads=%" PRIu32 " requests=%" PRIu64
           " ips=%" PRIu32 " seed=%" PRIu64 " allowed=%" PRIu64
           " denied=%" PRIu64 " errors=%" PRIu64 " allowed_tokens=%" PRIu64
           " elapsed_ns=%" PRIu64 " ops_per_sec=%.0f valid=%s\n",
           argv[1], argv[4], argv[6], threads_count, total, ip_count, seed,
           allowed, denied, errors, spent, end - start,
           (double)total * 1000000000.0 / (double)(end - start),
           valid ? "yes" : "no");
cleanup:
    for (i = 0U; i < threads_count && i < MAX_THREADS; ++i) free(workers[i].spent_by_ip);
    mutex_rate_limiter_destroy(&mtx);
    ip_rate_limiter_destroy(&lf);
    return valid ? 0 : 1;
}
