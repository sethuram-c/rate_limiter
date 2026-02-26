#include "rate_service.h"
#include "monotonic_clock.h"

#include <stddef.h>

#define NS_PER_MS UINT64_C(1000000)

bool rate_service_monotonic_ms(void *context, uint64_t *now_ms)
{
    uint64_t now_ns;
    (void)context;

    if (now_ms == NULL || monotonic_clock_now_ns(&now_ns) != 0) {
        return false;
    }
    *now_ms = now_ns / NS_PER_MS;
    return true;
}

static bool service_is_empty(const rate_service_t *service)
{
    return service != NULL && service->clock == NULL &&
           service->clock_context == NULL &&
           service->limiter.buckets == NULL &&
           service->limiter.index.slots == NULL &&
           service->limiter.index.capacity == 0U;
}

bool rate_service_init(rate_service_t *service, size_t slots,
                       const bucket_policy_t *policy,
                       rate_service_clock_fn clock, void *clock_context)
{
    uint64_t now_ms;

    if (!service_is_empty(service) || policy == NULL || clock == NULL ||
        slots == 0U) {
        return false;
    }
    /* Any failure precedes index allocation and key publication. */
    if (!clock(clock_context, &now_ms) ||
        now_ms > BUCKET_STATE_MAX_TIME_MS) {
        return false;
    }
    if (!ip_rate_limiter_init(&service->limiter, slots, policy, now_ms)) {
        return false;
    }
    service->clock_context = clock_context;
    service->clock = clock;
    return true;
}

bool rate_service_init_monotonic(rate_service_t *service, size_t slots,
                                 const bucket_policy_t *policy)
{
    return rate_service_init(service, slots, policy,
                             rate_service_monotonic_ms, NULL);
}

void rate_service_destroy(rate_service_t *service)
{
    if (service != NULL) {
        ip_rate_limiter_destroy(&service->limiter);
        service->clock = NULL;
        service->clock_context = NULL;
    }
}

rate_service_result_t rate_service_check(rate_service_t *service,
                                         uint32_t ipv4, uint32_t tokens)
{
    ip_rate_decision_t decision;
    uint64_t now_ms;

    if (service == NULL || service->clock == NULL ||
        service->limiter.buckets == NULL ||
        service->limiter.index.slots == NULL || tokens == 0U) {
        return RATE_SERVICE_INVALID;
    }
    /* Read the clock BEFORE lookup/insertion: failure has no key side effect. */
    if (!service->clock(service->clock_context, &now_ms) ||
        now_ms > BUCKET_STATE_MAX_TIME_MS) {
        return RATE_SERVICE_CLOCK_ERROR;
    }
    decision = ip_rate_limiter_check(&service->limiter, ipv4, tokens, now_ms);
    switch (decision) {
    case IP_RATE_ALLOWED: return RATE_SERVICE_ALLOWED;
    case IP_RATE_DENIED: return RATE_SERVICE_DENIED;
    case IP_RATE_TABLE_FULL: return RATE_SERVICE_TABLE_FULL;
    case IP_RATE_INVALID: return RATE_SERVICE_INVALID;
    }
    return RATE_SERVICE_INVALID;
}
