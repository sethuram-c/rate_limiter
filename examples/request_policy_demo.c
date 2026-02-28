#include "request_policy.h"

#include <stdint.h>
#include <stdio.h>

typedef struct { uint64_t now; } demo_clock_t;

static bool sample_ms(void *context, uint64_t *out)
{
    const demo_clock_t *clock = context;
    if (clock == NULL || out == NULL) return false;
    *out = clock->now;
    return true;
}

static const char *route_name(request_policy_route_t route)
{
    switch (route) {
    case REQUEST_POLICY_DELEGATED: return "RATE_LIMITER";
    case REQUEST_POLICY_BYPASSED: return "BYPASS_RULE";
    case REQUEST_POLICY_BLOCKED: return "BLOCK_RULE";
    }
    return "UNKNOWN";
}

int main(void)
{
    request_policy_t policy = {0};
    rate_service_t service = {0};
    demo_clock_t clock = {1000U};
    const bucket_policy_t bucket = {2U, 100U};
    const struct {
        const char *ip;
        unsigned int cost;
    } requests[] = {
        {"192.0.2.10", 1U}, {"192.0.2.20", 1U},
        {"198.51.100.9", 1U}, {"198.51.100.9", 1U},
        {"198.51.100.9", 1U}, {"bad-ip", 1U},
        {"192.0.2.10", 0U}, {"192.0.2.10", 100U}
    };
    size_t i;

    if (!request_policy_add_rule(&policy, "192.0.2.0/24", REQUEST_POLICY_BYPASS) ||
        !request_policy_add_rule(&policy, "192.0.2.20/32", REQUEST_POLICY_BLOCK) ||
        !rate_service_init(&service, 4U, &bucket, sample_ms, &clock)) {
        fputs("setup failed\n", stderr);
        return 1;
    }
    puts("Phase 17: immutable CIDR policy + original rate limiter");
    for (i = 0U; i < sizeof(requests) / sizeof(requests[0]); ++i) {
        request_policy_response_t reply;
        if (!request_policy_handle(&policy, &service, requests[i].ip,
                                   requests[i].cost, &reply)) {
            fputs("policy call failed\n", stderr);
            rate_service_destroy(&service);
            return 1;
        }
        printf("%-15s cost=%-3u route=%-12s status=%d forwarded=%d",
               requests[i].ip, requests[i].cost, route_name(reply.route),
               reply.http_status, reply.forwarded ? 1 : 0);
        if (reply.has_remaining) printf(" remaining=%u", reply.remaining_tokens);
        if (reply.has_retry_hint) printf(" retry_ms=%llu",
                                       (unsigned long long)reply.retry_after_ms);
        putchar('\n');
    }
    rate_service_destroy(&service);
    return 0;
}
