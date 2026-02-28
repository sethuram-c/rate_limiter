#include "rate_service.h"
#include "monotonic_clock.h"
#include "ip_table_plan.h"

#include <stddef.h>
#include <stdatomic.h>

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

static void reset_counters(rate_service_t *service)
{
    /* Init/destroy are quiescent-only, never concurrent with requests. */
    atomic_init(&service->counters.allowed, 0U);
    atomic_init(&service->counters.denied, 0U);
    atomic_init(&service->counters.table_full, 0U);
    atomic_init(&service->counters.invalid, 0U);
    atomic_init(&service->counters.clock_error, 0U);
}

static bool counters_are_lock_free(const rate_service_t *service)
{
    return atomic_is_lock_free(&service->counters.allowed) &&
           atomic_is_lock_free(&service->counters.denied) &&
           atomic_is_lock_free(&service->counters.table_full) &&
           atomic_is_lock_free(&service->counters.invalid) &&
           atomic_is_lock_free(&service->counters.clock_error);
}

static void record_result(rate_service_t *service,
                          rate_service_result_t result)
{
    _Atomic uint64_t *counter = NULL;
    switch (result) {
    case RATE_SERVICE_ALLOWED: counter = &service->counters.allowed; break;
    case RATE_SERVICE_DENIED: counter = &service->counters.denied; break;
    case RATE_SERVICE_TABLE_FULL: counter = &service->counters.table_full; break;
    case RATE_SERVICE_INVALID: counter = &service->counters.invalid; break;
    case RATE_SERVICE_CLOCK_ERROR: counter = &service->counters.clock_error; break;
    }
    if (counter != NULL) {
        (void)atomic_fetch_add_explicit(counter, 1U, memory_order_relaxed);
    }
}

rate_service_status_t rate_service_classify(rate_service_result_t result)
{
    switch (result) {
    case RATE_SERVICE_ALLOWED: return RATE_STATUS_ADMIT;
    case RATE_SERVICE_DENIED: return RATE_STATUS_THROTTLED;
    case RATE_SERVICE_TABLE_FULL: return RATE_STATUS_CAPACITY_EXHAUSTED;
    case RATE_SERVICE_INVALID: return RATE_STATUS_BAD_REQUEST;
    case RATE_SERVICE_CLOCK_ERROR: return RATE_STATUS_CLOCK_UNAVAILABLE;
    }
    return RATE_STATUS_UNKNOWN;
}

const char *rate_service_result_name(rate_service_result_t result)
{
    switch (result) {
    case RATE_SERVICE_ALLOWED: return "ALLOWED";
    case RATE_SERVICE_DENIED: return "DENIED";
    case RATE_SERVICE_TABLE_FULL: return "TABLE_FULL";
    case RATE_SERVICE_INVALID: return "INVALID";
    case RATE_SERVICE_CLOCK_ERROR: return "CLOCK_ERROR";
    }
    return "UNKNOWN";
}

bool rate_service_get_stats(const rate_service_t *service,
                            rate_service_stats_t *out)
{
    rate_service_stats_t snap;
    if (service == NULL || out == NULL || service->clock == NULL) {
        return false;
    }
    snap.allowed = atomic_load_explicit(&service->counters.allowed, memory_order_relaxed);
    snap.denied = atomic_load_explicit(&service->counters.denied, memory_order_relaxed);
    snap.table_full = atomic_load_explicit(&service->counters.table_full, memory_order_relaxed);
    snap.invalid = atomic_load_explicit(&service->counters.invalid, memory_order_relaxed);
    snap.clock_error = atomic_load_explicit(&service->counters.clock_error, memory_order_relaxed);
    *out = snap;
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
    uint64_t validated_initial_state;

    if (!service_is_empty(service) || clock == NULL ||
        !ip_table_plan_valid_capacity(slots) ||
        !bucket_state_initial(policy, 0U, &validated_initial_state)) {
        return false;
    }
    /* Counter atomics must be lock-free too, to preserve the request path.
     * Check before calling the user clock or allocating memory. */
    reset_counters(service);
    if (!counters_are_lock_free(service)) {
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

bool rate_service_init_with_config(rate_service_t *service,
                                   const rate_config_t *config,
                                   rate_service_clock_fn clock,
                                   void *clock_context)
{
    rate_config_plan_t plan;

    if (!service_is_empty(service) || clock == NULL ||
        rate_config_plan(config, &plan) != RATE_CONFIG_OK) {
        return false;
    }
    /* Existing initializer samples the clock only after basic validation.
     * The allocation budget is already checked in the local plan. */
    return rate_service_init(service, plan.slots, &plan.policy,
                             clock, clock_context);
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
    rate_service_result_t outcome;
    uint64_t now_ms;

    /* A nonexistent/uninitialized service has no valid counter bank. */
    if (service == NULL || service->clock == NULL ||
        service->limiter.buckets == NULL ||
        service->limiter.index.slots == NULL) {
        return RATE_SERVICE_INVALID;
    }
    if (tokens == 0U) {
        record_result(service, RATE_SERVICE_INVALID);
        return RATE_SERVICE_INVALID;
    }
    /* Read the clock BEFORE lookup/insertion: error cannot claim an IP. */
    if (!service->clock(service->clock_context, &now_ms) ||
        now_ms > BUCKET_STATE_MAX_TIME_MS) {
        record_result(service, RATE_SERVICE_CLOCK_ERROR);
        return RATE_SERVICE_CLOCK_ERROR;
    }
    decision = ip_rate_limiter_check(&service->limiter, ipv4, tokens, now_ms);
    switch (decision) {
    case IP_RATE_ALLOWED: outcome = RATE_SERVICE_ALLOWED; break;
    case IP_RATE_DENIED: outcome = RATE_SERVICE_DENIED; break;
    case IP_RATE_TABLE_FULL: outcome = RATE_SERVICE_TABLE_FULL; break;
    case IP_RATE_INVALID: outcome = RATE_SERVICE_INVALID; break;
    default: outcome = RATE_SERVICE_INVALID; break;
    }
    record_result(service, outcome);
    return outcome;
}

/* Map lower-layer results without adding a second state read. */
static rate_service_result_t map_decision(ip_rate_decision_t decision)
{
    switch (decision) {
    case IP_RATE_ALLOWED: return RATE_SERVICE_ALLOWED;
    case IP_RATE_DENIED: return RATE_SERVICE_DENIED;
    case IP_RATE_TABLE_FULL: return RATE_SERVICE_TABLE_FULL;
    case IP_RATE_INVALID: return RATE_SERVICE_INVALID;
    }
    return RATE_SERVICE_INVALID;
}

bool rate_service_check_detailed(rate_service_t *service, uint32_t ipv4,
                                 uint32_t tokens,
                                 rate_service_diagnostic_t *out)
{
    rate_service_diagnostic_t report = {0};
    ip_rate_report_t lower;
    uint64_t sample;

    if (out == NULL) {
        return false;
    }
    report.result = RATE_SERVICE_INVALID;
    if (service == NULL || service->clock == NULL ||
        service->limiter.buckets == NULL ||
        service->limiter.index.slots == NULL) {
        *out = report;
        return true;
    }
    if (tokens == 0U) {
        *out = report;
        record_result(service, report.result);
        return true;
    }
    if (!service->clock(service->clock_context, &sample) ||
        sample > BUCKET_STATE_MAX_TIME_MS) {
        report.result = RATE_SERVICE_CLOCK_ERROR;
        *out = report;
        record_result(service, report.result);
        return true;
    }
    (void)ip_rate_limiter_check_report(&service->limiter, ipv4, tokens,
                                      sample, &lower);
    report.result = map_decision(lower.decision);
    if (lower.valid_state) {
        uint64_t anchor;
        uint16_t remaining;
        uint64_t deficit;
        bucket_state_unpack(lower.committed_state, &anchor, &remaining);
        report.has_bucket_state = true;
        report.sampled_now_ms = sample;
        report.refill_anchor_ms = anchor;
        report.remaining_tokens = remaining;
        report.capacity = lower.policy.capacity;
        report.refill_interval_ms = lower.policy.refill_interval_ms;

        if (report.result == RATE_SERVICE_DENIED &&
            tokens <= (uint32_t)lower.policy.capacity) {
            uint64_t wait_until;
            deficit = (uint64_t)tokens - (uint64_t)remaining;
            /* Prevent producing a retry time outside the 48-bit clock range.
             * deficit >= 1, interval >= 1 for initialized policies. */
            if (deficit <= (BUCKET_STATE_MAX_TIME_MS - anchor) /
                           lower.policy.refill_interval_ms) {
                wait_until = anchor + deficit * lower.policy.refill_interval_ms;
                if (wait_until > sample) {
                    report.retry_possible = true;
                    report.retry_after_ms = wait_until - sample;
                }
            }
        }
    }
    *out = report;
    record_result(service, report.result);
    return true;
}
