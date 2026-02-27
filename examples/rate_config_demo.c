#include "rate_config.h"
#include "rate_service.h"

#include <inttypes.h>
#include <stdio.h>

static bool fixed_clock(void *context, uint64_t *now_ms)
{
    (void)context;
    if (now_ms == NULL) {
        return false;
    }
    *now_ms = 5000U;
    return true;
}

int main(void)
{
    rate_config_t config = {64U, 5U, 250U, 4096U};
    rate_config_plan_t plan;
    rate_config_status_t status = rate_config_plan(&config, &plan);
    rate_service_t service = {0};

    if (status != RATE_CONFIG_OK) {
        fprintf(stderr, "config rejected: %s\n",
                rate_config_status_string(status));
        return 1;
    }
    printf("64 IP slots: keys=%zu bytes, buckets=%zu bytes, total=%zu bytes\n",
           plan.key_array_bytes, plan.bucket_array_bytes,
           plan.total_array_bytes);
    if (!rate_service_init_with_config(&service, &config, fixed_clock, NULL)) {
        fputs("service could not initialize\n", stderr);
        return 1;
    }
    printf("IP 192.0.2.1 first request: %s\n",
           rate_service_check(&service, UINT32_C(0xC0000201), 1U) ==
           RATE_SERVICE_ALLOWED ? "allowed" : "unexpected result");
    rate_service_destroy(&service);

    config.memory_budget_bytes = plan.total_array_bytes - 1U;
    status = rate_config_plan(&config, &plan);
    printf("one byte below budget: %s\n", rate_config_status_string(status));
    return status == RATE_CONFIG_OVER_BUDGET ? 0 : 1;
}
