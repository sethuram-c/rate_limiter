#include "rate_service.h"

#include <inttypes.h>
#include <stdio.h>
#include <string.h>

/* Deterministic clock for a reproducible end-to-end request trace. */
typedef struct { uint64_t now_ms; } demo_clock_t;

static bool demo_now(void *context, uint64_t *now_ms)
{
    const demo_clock_t *clock = context;
    if (clock == NULL || now_ms == NULL) {
        return false;
    }
    *now_ms = clock->now_ms;
    return true;
}

static const char *label(rate_service_result_t result)
{
    switch (result) {
    case RATE_SERVICE_ALLOWED: return "ALLOW";
    case RATE_SERVICE_DENIED: return "DENY";
    case RATE_SERVICE_TABLE_FULL: return "TABLE_FULL";
    case RATE_SERVICE_INVALID: return "INVALID";
    case RATE_SERVICE_CLOCK_ERROR: return "CLOCK_ERROR";
    }
    return "UNKNOWN";
}

int main(int argc, char **argv)
{
    bucket_policy_t policy = { 2U, 100U }; /* 2-token burst; 1 per 100 ms */
    rate_service_t service = {0};
    demo_clock_t demo_clock = {1000U};
    bool live = argc == 2 && strcmp(argv[1], "--live") == 0;
    uint32_t ips[] = { UINT32_C(0xC0000201), UINT32_C(0xC0000201),
                       UINT32_C(0xC0000201), UINT32_C(0xC6336402),
                       UINT32_C(0xC0000201) };
    size_t i;

    if (argc > 2 || (argc == 2 && !live)) {
        fprintf(stderr, "usage: %s [--live]\n", argv[0]);
        return 2;
    }
    if (!(live ? rate_service_init_monotonic(&service, 8U, &policy) :
                 rate_service_init(&service, 8U, &policy,
                                   demo_now, &demo_clock))) {
        fprintf(stderr, "service initialization failed\n");
        return 1;
    }
    printf("mode=%s\n", live ? "live-monotonic" : "injected-clock");
    for (i = 0U; i < sizeof(ips) / sizeof(ips[0]); ++i) {
        rate_service_result_t result;
        if (!live && i == 4U) {
            demo_clock.now_ms += 100U;
        }
        result = rate_service_check(&service, ips[i], 1U);
        printf("request=%zu ip=0x%08" PRIx32 " result=%s\n",
               i + 1U, ips[i], label(result));
        if (result != RATE_SERVICE_ALLOWED && result != RATE_SERVICE_DENIED) {
            rate_service_destroy(&service);
            return 1;
        }
    }
    rate_service_destroy(&service);
    return 0;
}
