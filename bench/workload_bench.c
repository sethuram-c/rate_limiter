#include "rate_service.h"
#include <inttypes.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <stdint.h>

#define MAX_THREADS 16U
#define MAX_REQUESTS 10000000U

typedef struct {
    rate_service_t *service;
    uint64_t iterations;
    uint32_t ip;
    int shared;
    uint64_t admitted;
    uint64_t denied;
    uint64_t other;
} worker_t;

static bool fixed_clock(void *ctx, uint64_t *out) {
    *out = *(const uint64_t *)ctx;
    return true;
}

static uint64_t monotonic_ns(void) {
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) {
        perror("clock_gettime");
        exit(2);
    }
    return (uint64_t)ts.tv_sec * UINT64_C(1000000000) + (uint64_t)ts.tv_nsec;
}

static void *run_worker(void *arg) {
    worker_t *w = arg;
    for (uint64_t i = 0; i < w->iterations; ++i) {
        const uint32_t ip = w->shared ? 1U : w->ip;
        rate_service_result_t r = rate_service_check(w->service, ip, 1U);
        if (r == RATE_SERVICE_ALLOWED) ++w->admitted;
        else if (r == RATE_SERVICE_DENIED) ++w->denied;
        else ++w->other;
    }
    return NULL;
}

static int parse_u32(const char *s, unsigned long *out) {
    char *end = NULL;
    if (s[0] == '-') return 0;
    *out = strtoul(s, &end, 10);
    return end != s && *end == '\0';
}

int main(int argc, char **argv) {
    unsigned long threads = 0, requests = 0;
    if (argc != 4 || !parse_u32(argv[1], &threads) ||
        !parse_u32(argv[2], &requests) ||
        (strcmp(argv[3], "shared") != 0 && strcmp(argv[3], "sharded") != 0) ||
        threads < 1 || threads > MAX_THREADS || requests < 1 || requests > MAX_REQUESTS) {
        fprintf(stderr, "usage: %s THREADS(1..16) REQUESTS(1..10000000) shared|sharded\n", argv[0]);
        return 2;
    }
    const int shared = strcmp(argv[3], "shared") == 0;
    uint64_t now = UINT64_C(1000000);
    bucket_policy_t policy = {65535U, 100U};
    rate_service_t service = {0};
    if (!rate_service_init(&service, 64U, &policy, fixed_clock, &now)) {
        fputs("service initialization failed\n", stderr);
        return 1;
    }
    /* Warm up every key before timing: measure steady-state processing only. */
    for (unsigned long i = 0; i < (shared ? 1UL : threads); ++i) {
        if (rate_service_check(&service, (uint32_t)i + 1U, 1U) != RATE_SERVICE_ALLOWED) {
            fputs("warmup failed\n", stderr);
            rate_service_destroy(&service);
            return 1;
        }
    }
    pthread_t handles[MAX_THREADS];
    worker_t workers[MAX_THREADS] = {{0}};
    uint64_t base = requests / threads;
    uint64_t remainder = requests % threads;
    for (unsigned long i = 0; i < threads; ++i) {
        workers[i].service = &service;
        workers[i].iterations = base + (i < remainder ? 1U : 0U);
        workers[i].ip = (uint32_t)i + 1U;
        workers[i].shared = shared;
    }
    uint64_t start = monotonic_ns();
    unsigned long launched = 0;
    for (unsigned long i = 0; i < threads; ++i) {
        if (pthread_create(&handles[i], NULL, run_worker, &workers[i]) != 0) {
            fputs("pthread_create failed\n", stderr);
            for (unsigned long j = 0; j < launched; ++j) (void)pthread_join(handles[j], NULL);
            rate_service_destroy(&service);
            return 1;
        }
        ++launched;
    }
    for (unsigned long i = 0; i < threads; ++i) (void)pthread_join(handles[i], NULL);
    uint64_t elapsed_ns = monotonic_ns() - start;
    uint64_t allowed = 0, denied = 0, errors = 0;
    for (unsigned long i = 0; i < threads; ++i) {
        allowed += workers[i].admitted;
        denied += workers[i].denied;
        errors += workers[i].other;
    }
    const uint64_t expected_allowed =
        shared ? (requests < 65534UL ? requests : 65534UL) :
        (requests < threads * 65534UL ? requests : threads * 65534UL);
    const int ok = (allowed + denied + errors == requests && errors == 0U &&
                    allowed == expected_allowed);
    printf("scenario=%s threads=%lu requests=%lu allowed=%" PRIu64
           " denied=%" PRIu64 " errors=%" PRIu64
           " elapsed_ns=%" PRIu64 " ops_per_sec=%.0f valid=%s\n",
           argv[3], threads, requests, allowed, denied, errors, elapsed_ns,
           elapsed_ns ? (double)requests * 1000000000.0 / (double)elapsed_ns : 0.0,
           ok ? "yes" : "no");
    rate_service_destroy(&service);
    return ok ? 0 : 1;
}
