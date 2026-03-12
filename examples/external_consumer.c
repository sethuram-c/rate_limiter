/*
 * Phase 29: standalone program linked only to the installed library.
 * Build against staged headers/library (see tests/test_library_package.sh).
 */
#include "ratelimiter.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

/* Only this single-threaded example mutates the clock without atomics.
 * Applications with worker threads must provide thread-safe callbacks. */
typedef struct { uint64_t now_ms; } fixture_clock_t;

static bool fixture_now(void *context, uint64_t *out)
{
    const fixture_clock_t *clock = context;
    if (clock == NULL || out == NULL) {
        return false;
    }
    *out = clock->now_ms;
    return true;
}

static bool expect_status(rate_service_t *service, const char *ip, size_t length,
                          int expected_status)
{
    request_gateway_response_t response;
    if (!request_gateway_handle_n(service, ip, length, 1U, &response)) {
        return false;
    }
    return response.http_status == expected_status;
}

int main(void)
{
    const bucket_policy_t policy = {2U, 100U};
    const char ip1[] = {'1','9','2','.','0','.','2','.','1'};
    const char ip2[] = {'1','9','8','.','5','1','.','1','0','0','.','7'};
    fixture_clock_t clock = {1000U};
    rate_service_t service = {0};
    rate_service_stats_t stats;
    bool passed = true;

    if (!rate_service_init(&service, 8U, &policy, fixture_now, &clock)) {
        fputs("failed to initialize rate-service consumer\n", stderr);
        return 1;
    }
    passed = passed && expect_status(&service, ip1, sizeof ip1, 200);
    passed = passed && expect_status(&service, ip1, sizeof ip1, 200);
    passed = passed && expect_status(&service, ip1, sizeof ip1, 429);
    passed = passed && expect_status(&service, ip2, sizeof ip2, 200);

    clock.now_ms = 1100U;
    passed = passed && expect_status(&service, ip1, sizeof ip1, 200);
    /* Pre-ingress validation should not increment service counters. */
    passed = passed && expect_status(&service, "bad", 3U, 400);
    passed = passed && rate_service_get_stats(&service, &stats);
    if (passed) {
        passed = stats.allowed == 4U && stats.denied == 1U &&
                 stats.invalid == 0U && stats.table_full == 0U &&
                 stats.clock_error == 0U;
    }
    rate_service_destroy(&service);
    if (!passed) {
        fputs("external consumer integration FAILED\n", stderr);
        return 1;
    }
    puts("external consumer integration passed: 4 allowed, 1 denied, 1 ingress rejection");
    return 0;
}
