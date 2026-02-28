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

static const char *name(rate_service_result_t value)
{
    switch (value) {
    case RATE_SERVICE_ALLOWED: return "allowed";
    case RATE_SERVICE_DENIED: return "denied";
    case RATE_SERVICE_TABLE_FULL: return "table-full";
    case RATE_SERVICE_INVALID: return "invalid";
    case RATE_SERVICE_CLOCK_ERROR: return "clock-error";
    }
    return "unknown";
}

int main(void)
{
    rate_service_t service = {0};
    bucket_policy_t policy = {2U, 100U};
    uint64_t now_ms = 1000U;
    unsigned i;

    assert(rate_service_init(&service, 4U, &policy, fake_clock, &now_ms));
    puts("Phase 14: decisions and CAS-consistent diagnostics");
    for (i = 0U; i < 4U; ++i) {
        rate_service_diagnostic_t report;
        assert(rate_service_check_detailed(&service, UINT32_C(0xC0000201),
                                            1U, &report));
        printf("request %u: %-7s remaining=%u retry_possible=%s retry_ms=%llu\n",
               i + 1U, name(report.result),
               (unsigned)report.remaining_tokens,
               report.retry_possible ? "yes" : "no",
               (unsigned long long)report.retry_after_ms);
    }
    now_ms = 1100U;
    {
        rate_service_diagnostic_t report;
        assert(rate_service_check_detailed(&service, UINT32_C(0xC0000201),
                                            1U, &report));
        printf("after 100ms: %s remaining=%u\n", name(report.result),
               (unsigned)report.remaining_tokens);
    }
    rate_service_destroy(&service);
    return 0;
}
