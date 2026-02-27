#ifndef RATE_CONFIG_H
#define RATE_CONFIG_H

#include "bucket_state.h"

#include <stddef.h>
#include <stdint.h>

/*
 * Phase 13: explicit configuration validation and allocation planning.
 *
 * memory_budget_bytes caps the two PAYLOAD arrays (key slots and buckets).
 * It does not include malloc allocator metadata, the service object, the
 * application, thread stacks, or other process allocations. For a strict
 * process memory cap, combine this check with an OS-level limit.
 *
 * Validate while quiescent. The resulting policy is immutable while workers
 * use the service. Zero budget is not a special "unlimited" value.
 */
typedef struct {
    size_t slots;               /* required: power of two, >= 1 */
    uint32_t capacity_tokens;   /* required: 1 ... UINT16_MAX */
    uint64_t refill_interval_ms;/* required: 1 ... BUCKET_STATE_MAX_TIME_MS */
    size_t memory_budget_bytes; /* required: >= required array payload bytes */
} rate_config_t;

typedef struct {
    size_t slots;
    size_t key_array_bytes;
    size_t bucket_array_bytes;
    size_t total_array_bytes;
    bucket_policy_t policy;
} rate_config_plan_t;

typedef enum {
    RATE_CONFIG_OK,
    RATE_CONFIG_NULL_ARGUMENT,
    RATE_CONFIG_INVALID_SLOTS,
    RATE_CONFIG_INVALID_CAPACITY,
    RATE_CONFIG_INVALID_REFILL,
    RATE_CONFIG_SIZE_OVERFLOW,
    RATE_CONFIG_OVER_BUDGET
} rate_config_status_t;

/* No allocation, clock access or side effects. On failure *out is untouched.
 * Totals use sizeof actual implementation types on THIS target/ABI. */
rate_config_status_t rate_config_plan(const rate_config_t *config,
                                      rate_config_plan_t *out);

/* Constant human-readable status; usable in command-line diagnostics. */
const char *rate_config_status_string(rate_config_status_t status);

#endif
