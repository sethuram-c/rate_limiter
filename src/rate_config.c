#include "rate_config.h"

#include "ip_atomic_table.h"
#include "ip_table_plan.h"
#include "lockfree_bucket.h"

#include <stdint.h>

rate_config_status_t rate_config_plan(const rate_config_t *config,
                                      rate_config_plan_t *out)
{
    rate_config_plan_t candidate;

    if (config == NULL || out == NULL) {
        return RATE_CONFIG_NULL_ARGUMENT;
    }
    if (!ip_table_plan_valid_capacity(config->slots)) {
        return RATE_CONFIG_INVALID_SLOTS;
    }
    if (config->capacity_tokens == 0U ||
        config->capacity_tokens > UINT16_MAX) {
        return RATE_CONFIG_INVALID_CAPACITY;
    }
    if (config->refill_interval_ms == 0U ||
        config->refill_interval_ms > BUCKET_STATE_MAX_TIME_MS) {
        return RATE_CONFIG_INVALID_REFILL;
    }
    if (config->slots > SIZE_MAX / sizeof(ip_atomic_slot_t) ||
        config->slots > SIZE_MAX / sizeof(lf_bucket_t)) {
        return RATE_CONFIG_SIZE_OVERFLOW;
    }

    candidate.key_array_bytes = config->slots * sizeof(ip_atomic_slot_t);
    candidate.bucket_array_bytes = config->slots * sizeof(lf_bucket_t);
    if (candidate.key_array_bytes > SIZE_MAX - candidate.bucket_array_bytes) {
        return RATE_CONFIG_SIZE_OVERFLOW;
    }
    candidate.total_array_bytes = candidate.key_array_bytes +
                                  candidate.bucket_array_bytes;
    if (candidate.total_array_bytes > config->memory_budget_bytes) {
        return RATE_CONFIG_OVER_BUDGET;
    }

    candidate.slots = config->slots;
    candidate.policy.capacity = (uint16_t)config->capacity_tokens;
    candidate.policy.refill_interval_ms = config->refill_interval_ms;
    *out = candidate;
    return RATE_CONFIG_OK;
}

const char *rate_config_status_string(rate_config_status_t status)
{
    switch (status) {
    case RATE_CONFIG_OK: return "valid configuration";
    case RATE_CONFIG_NULL_ARGUMENT: return "null configuration or output";
    case RATE_CONFIG_INVALID_SLOTS: return "slots must be a power of two";
    case RATE_CONFIG_INVALID_CAPACITY: return "capacity must be 1..65535";
    case RATE_CONFIG_INVALID_REFILL: return "refill interval out of range";
    case RATE_CONFIG_SIZE_OVERFLOW: return "size computation would overflow";
    case RATE_CONFIG_OVER_BUDGET: return "array payload exceeds memory budget";
    }
    return "unknown configuration error";
}
