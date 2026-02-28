#include "request_gateway.h"

#include <stdbool.h>
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>

/* The example is single-threaded: modifying its clock between events is safe.
 * A real concurrent clock callback must synchronize its shared context. */
typedef struct {
    uint64_t now_ms;
    bool fail;
} demo_clock_t;

typedef struct {
    uint64_t now_ms;
    const char *ip;
    uint32_t tokens;
    bool fail_clock;
} simulated_request_t;

static bool demo_now(void *context, uint64_t *now_ms)
{
    demo_clock_t *clock = context;
    if (clock == NULL || now_ms == NULL || clock->fail) {
        return false;
    }
    *now_ms = clock->now_ms;
    return true;
}

int main(void)
{
    const bucket_policy_t policy = {2U, 100U};
    const simulated_request_t workload[] = {
        {1000U, "192.0.2.1", 1U, false},
        {1000U, "192.0.2.1", 1U, false},
        {1000U, "192.0.2.1", 1U, false},
        {1000U, "198.51.100.7", 2U, false},
        {1050U, "192.0.2.1", 1U, false},
        {1100U, "192.0.2.1", 1U, false},
        {1100U, "not-an-ip", 1U, false},
        {1100U, "203.0.113.9", 1U, false},
        {1100U, "198.51.100.7", 1U, true},
        {1200U, "198.51.100.7", 1U, false},
        {1200U, "192.0.2.1", 0U, false}
    };
    const size_t count = sizeof workload / sizeof workload[0];
    rate_service_t service = {0};
    demo_clock_t clock = {1000U, false};
    rate_service_stats_t stats;
    size_t i;

    if (!rate_service_init(&service, 2U, &policy, demo_now, &clock)) {
        fputs("rate service initialization failed\n", stderr);
        return 1;
    }
    puts("Phase 16: deterministic HTTP-style request simulator (no network server)");
    puts(" time  client-ip          cost  status  result         remaining  retry-ms");
    for (i = 0U; i < count; ++i) {
        request_gateway_response_t reply;
        clock.now_ms = workload[i].now_ms;
        clock.fail = workload[i].fail_clock;
        if (!request_gateway_handle(&service, workload[i].ip,
                                    workload[i].tokens, &reply)) {
            fputs("request handling failed\n", stderr);
            rate_service_destroy(&service);
            return 1;
        }
        printf("%5" PRIu64 "  %-17s %4" PRIu32 "   %3d    %-13s ",
               clock.now_ms, workload[i].ip, workload[i].tokens,
               reply.http_status, rate_service_result_name(reply.result));
        if (reply.has_remaining) {
            printf("%3u", (unsigned)reply.remaining_tokens);
        } else {
            printf("  -");
        }
        if (reply.has_retry_hint) {
            printf("        %" PRIu64, reply.retry_after_ms);
        } else {
            printf("        -");
        }
        if (!reply.forwarded) {
            printf("  (rejected at ingress)");
        }
        putchar('\n');
    }
    if (!rate_service_get_stats(&service, &stats)) {
        rate_service_destroy(&service);
        return 1;
    }
    printf("service counters: allowed=%" PRIu64 " denied=%" PRIu64
           " full=%" PRIu64 " invalid=%" PRIu64 " clock=%" PRIu64 "\n",
           stats.allowed, stats.denied, stats.table_full,
           stats.invalid, stats.clock_error);
    puts("Note: malformed IP was rejected before service counters; request timeline has 11 events.");
    rate_service_destroy(&service);
    return 0;
}
