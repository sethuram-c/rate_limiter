#include "rate_service.h"

#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

static bool fake_clock(void *context, uint64_t *now_ms)
{
    if (context == NULL || now_ms == NULL) {
        return false;
    }
    *now_ms = *(const uint64_t *)context;
    return true;
}

static const char *category_name(rate_service_status_t category)
{
    switch (category) {
    case RATE_STATUS_ADMIT: return "ADMIT";
    case RATE_STATUS_THROTTLED: return "THROTTLED";
    case RATE_STATUS_CAPACITY_EXHAUSTED: return "CAPACITY_EXHAUSTED";
    case RATE_STATUS_BAD_REQUEST: return "BAD_REQUEST";
    case RATE_STATUS_CLOCK_UNAVAILABLE: return "CLOCK_UNAVAILABLE";
    case RATE_STATUS_UNKNOWN: return "UNKNOWN";
    }
    return "UNKNOWN";
}

static void print_request(rate_service_t *service, uint32_t ip, uint32_t amount)
{
    rate_service_result_t result = rate_service_check(service, ip, amount);
    printf("IP=%u amount=%u => %-11s (category: %s)\n",
           (unsigned)ip, (unsigned)amount,
           rate_service_result_name(result),
           category_name(rate_service_classify(result)));
}

int main(void)
{
    rate_service_t service = {0};
    bucket_policy_t policy = {2U, 100U};
    uint64_t now_ms = 1000U;
    rate_service_stats_t stats;

    assert(rate_service_init(&service, 2U, &policy, fake_clock, &now_ms));
    puts("Phase 15: operational policy outcomes and counters");
    print_request(&service, 1U, 1U);
    print_request(&service, 1U, 1U);
    print_request(&service, 1U, 1U);
    print_request(&service, 2U, 1U);
    print_request(&service, 3U, 1U);
    print_request(&service, 1U, 0U);
    assert(rate_service_get_stats(&service, &stats));
    printf("stats: allowed=%llu denied=%llu full=%llu invalid=%llu clock=%llu\n",
           (unsigned long long)stats.allowed,
           (unsigned long long)stats.denied,
           (unsigned long long)stats.table_full,
           (unsigned long long)stats.invalid,
           (unsigned long long)stats.clock_error);
    rate_service_destroy(&service);
    return 0;
}
