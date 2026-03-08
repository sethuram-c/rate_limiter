/* Phase 18: request-path throughput harness. Synthetic in-process benchmark;
 * this is NOT HTTP throughput or a cross-host performance guarantee. */
#include "rate_service.h"
#include "traffic_workload.h"
#include "monotonic_clock.h"

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

typedef struct {
    traffic_workload_t workload;
    uint32_t threads;
    uint64_t requests_per_thread;
    uint16_t capacity;
    bool use_real_clock;
    bool csv;
} bench_options_t;

typedef struct {
    rate_service_t *service;
    const traffic_workload_t *workload;
    _Atomic bool *start;
    _Atomic bool *abort_run;
    uint32_t worker_id;
    uint64_t requests;
    bool succeeded;
} worker_t;

static void usage(const char *name)
{
    fprintf(stderr,
            "Usage: %s [--threads N] [--requests N] [--ips N] "
            "[--pattern hot|uniform|skewed]\n"
            "  [--seed N] [--capacity N] [--clock fixed|monotonic] [--csv]\n"
            "  --requests is PER WORKER, --capacity is per-IP, default clock=fixed.\n",
            name);
}

static bool parse_uint(const char *value, uint64_t max, uint64_t *out)
{
    char *end;
    unsigned long long parsed;
    if (value == NULL || *value == '\0' || *value == '-') return false;
    errno = 0;
    end = NULL;
    parsed = strtoull(value, &end, 10);
    if (errno != 0 || end == value || *end != '\0' || (uint64_t)parsed > max) {
        return false;
    }
    *out = (uint64_t)parsed;
    return true;
}

static bool parse_options(int argc, char **argv, bench_options_t *opt)
{
    int i;
    *opt = (bench_options_t){{UINT64_C(20270101), 256U, TRAFFIC_UNIFORM},
                             4U, UINT64_C(25000), 32U, false, false};
    for (i = 1; i < argc; ++i) {
        uint64_t value;
        if (strcmp(argv[i], "--csv") == 0) {
            opt->csv = true;
            continue;
        }
        if (i + 1 >= argc) return false;
        if (strcmp(argv[i], "--pattern") == 0) {
            ++i;
            if (strcmp(argv[i], "hot") == 0) opt->workload.pattern = TRAFFIC_HOT;
            else if (strcmp(argv[i], "uniform") == 0) opt->workload.pattern = TRAFFIC_UNIFORM;
            else if (strcmp(argv[i], "skewed") == 0) opt->workload.pattern = TRAFFIC_SKEWED;
            else return false;
        } else if (strcmp(argv[i], "--clock") == 0) {
            ++i;
            if (strcmp(argv[i], "fixed") == 0) opt->use_real_clock = false;
            else if (strcmp(argv[i], "monotonic") == 0) opt->use_real_clock = true;
            else return false;
        } else {
            if (!parse_uint(argv[i + 1], UINT64_MAX, &value)) return false;
            if (strcmp(argv[i], "--threads") == 0 && value >= 1U && value <= 64U)
                opt->threads = (uint32_t)value;
            else if (strcmp(argv[i], "--requests") == 0 && value >= 1U && value <= UINT64_C(10000000))
                opt->requests_per_thread = value;
            else if (strcmp(argv[i], "--ips") == 0 && value >= 1U && value <= 65536U)
                opt->workload.ip_count = (uint32_t)value;
            else if (strcmp(argv[i], "--seed") == 0)
                opt->workload.seed = value;
            else if (strcmp(argv[i], "--capacity") == 0 && value >= 1U && value <= UINT16_MAX)
                opt->capacity = (uint16_t)value;
            else return false;
            ++i;
        }
    }
    return traffic_workload_valid(&opt->workload);
}

static bool fixed_clock(void *context, uint64_t *out_ms)
{
    const uint64_t *fixed_ms = context;
    if (out_ms == NULL || fixed_ms == NULL) return false;
    *out_ms = *fixed_ms;
    return true;
}

static void *worker_main(void *arg)
{
    worker_t *worker = arg;
    uint64_t i;
    worker->succeeded = true;
    while (!atomic_load_explicit(worker->start, memory_order_acquire)) {
        sched_yield();
    }
    if (atomic_load_explicit(worker->abort_run, memory_order_relaxed)) return NULL;
    for (i = 0U; i < worker->requests; ++i) {
        uint32_t ip;
        rate_service_result_t result;
        if (!traffic_workload_ip(worker->workload, worker->worker_id, i, &ip)) {
            worker->succeeded = false;
            break;
        }
        result = rate_service_check(worker->service, ip, 1U);
        if (result != RATE_SERVICE_ALLOWED && result != RATE_SERVICE_DENIED) {
            worker->succeeded = false;
            break;
        }
    }
    return NULL;
}

/* Independently derive fixed-clock allowance from the traffic sequence.
 * Derivation runs AFTER the measured interval; never pollute the timing. */
static bool expected_fixed_allows(const bench_options_t *opt, uint64_t *out)
{
    uint64_t *frequency = calloc(opt->workload.ip_count, sizeof(*frequency));
    uint64_t result = 0U;
    uint32_t worker_id;
    if (frequency == NULL) return false;
    for (worker_id = 0U; worker_id < opt->threads; ++worker_id) {
        uint64_t i;
        for (i = 0U; i < opt->requests_per_thread; ++i) {
            uint32_t ip;
            if (!traffic_workload_ip(&opt->workload, worker_id, i, &ip)) {
                free(frequency);
                return false;
            }
            ++frequency[ip - UINT32_C(0xc6120000)];
        }
    }
    for (worker_id = 0U; worker_id < opt->workload.ip_count; ++worker_id) {
        result += frequency[worker_id] < opt->capacity ?
                  frequency[worker_id] : opt->capacity;
    }
    free(frequency);
    *out = result;
    return true;
}

static size_t table_capacity(uint32_t ip_count)
{
    size_t slots = 2U;
    while (slots < (size_t)ip_count * 2U) slots *= 2U;
    return slots;
}

int main(int argc, char **argv)
{
    bench_options_t opt;
    rate_service_t service = {0};
    pthread_t *threads = NULL;
    worker_t *workers = NULL;
    _Atomic bool start = false;
    _Atomic bool abort_run = false;
    const uint64_t fixed_ms = UINT64_C(1000000);
    const bucket_policy_t policy = {32U, UINT64_C(60000)};
    bucket_policy_t selected_policy = policy;
    rate_service_stats_t stats;
    uint64_t before_ns = 0U, after_ns = 0U, total, elapsed_ns, expected = 0U;
    uint32_t created = 0U, i;
    bool ok = true;

    if (!parse_options(argc, argv, &opt)) {
        usage(argv[0]);
        return EXIT_FAILURE;
    }
    selected_policy.capacity = opt.capacity;
    total = (uint64_t)opt.threads * opt.requests_per_thread;
    if (!rate_service_init(&service, table_capacity(opt.workload.ip_count),
                           &selected_policy,
                           opt.use_real_clock ? rate_service_monotonic_ms : fixed_clock,
                           opt.use_real_clock ? NULL : (void *)&fixed_ms)) {
        fputs("benchmark: service initialization failed\n", stderr);
        return EXIT_FAILURE;
    }
    threads = calloc(opt.threads, sizeof(*threads));
    workers = calloc(opt.threads, sizeof(*workers));
    if (threads == NULL || workers == NULL) {
        fputs("benchmark: worker allocation failed\n", stderr);
        ok = false;
    }
    if (ok) {
        for (i = 0U; i < opt.threads; ++i) {
            workers[i] = (worker_t){ &service, &opt.workload, &start, &abort_run,
                                     i, opt.requests_per_thread, false };
            if (pthread_create(&threads[i], NULL, worker_main, &workers[i]) != 0) {
                fputs("benchmark: pthread_create failed\n", stderr);
                ok = false;
                break;
            }
            ++created;
        }
    }
    if (!ok) atomic_store_explicit(&abort_run, true, memory_order_relaxed);
    if (ok && monotonic_clock_now_ns(&before_ns) != 0) {
        ok = false;
    }
    atomic_store_explicit(&start, true, memory_order_release);
    for (i = 0U; i < created; ++i) {
        if (pthread_join(threads[i], NULL) != 0) ok = false;
    }
    if (monotonic_clock_now_ns(&after_ns) != 0) ok = false;
    for (i = 0U; i < created; ++i) {
        if (!workers[i].succeeded) ok = false;
    }
    if (!rate_service_get_stats(&service, &stats)) ok = false;
    if (!ok || before_ns == 0U || after_ns < before_ns) {
        fputs("benchmark: incomplete or invalid run\n", stderr);
        free(threads);
        free(workers);
        rate_service_destroy(&service);
        return EXIT_FAILURE;
    }
    elapsed_ns = after_ns - before_ns;
    if (elapsed_ns == 0U) elapsed_ns = 1U;
    if (stats.allowed + stats.denied != total ||
        stats.table_full || stats.invalid || stats.clock_error) {
        fputs("benchmark: request accounting invariant failed\n", stderr);
        free(threads);
        free(workers);
        rate_service_destroy(&service);
        return EXIT_FAILURE;
    }
    if (!opt.use_real_clock) {
        if (!expected_fixed_allows(&opt, &expected) || expected != stats.allowed) {
            fputs("benchmark: fixed-clock admission oracle mismatch\n", stderr);
            free(threads);
            free(workers);
            rate_service_destroy(&service);
            return EXIT_FAILURE;
        }
    }
    if (opt.csv) {
        puts("pattern,clock,threads,requests_per_thread,ips,seed,capacity,total,allowed,denied,elapsed_ns,ops_per_second");
        printf("%s,%s,%" PRIu32 ",%" PRIu64 ",%" PRIu32 ",%" PRIu64
               ",%" PRIu16 ",%" PRIu64 ",%" PRIu64 ",%" PRIu64
               ",%" PRIu64 ",%.2f\n",
               traffic_pattern_name(opt.workload.pattern),
               opt.use_real_clock ? "monotonic" : "fixed",
               opt.threads, opt.requests_per_thread, opt.workload.ip_count,
               opt.workload.seed, opt.capacity, total, stats.allowed,
               stats.denied, elapsed_ns, (double)total * 1e9 / (double)elapsed_ns);
    } else {
        printf("pattern=%s clock=%s threads=%" PRIu32 " ips=%" PRIu32
               " requests=%" PRIu64 " allowed=%" PRIu64 " denied=%" PRIu64
               " elapsed_ms=%.3f throughput_ops_s=%.2f\n",
               traffic_pattern_name(opt.workload.pattern),
               opt.use_real_clock ? "monotonic" : "fixed",
               opt.threads, opt.workload.ip_count, total, stats.allowed,
               stats.denied, (double)elapsed_ns / 1e6,
               (double)total * 1e9 / (double)elapsed_ns);
        if (!opt.use_real_clock)
            printf("fixed-clock oracle: exactly %" PRIu64 " admissions verified\n", expected);
    }
    free(threads);
    free(workers);
    rate_service_destroy(&service);
    return EXIT_SUCCESS;
}
