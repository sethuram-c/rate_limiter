#ifndef RATE_SERVICE_H
#define RATE_SERVICE_H

#include "ip_rate_limiter.h"
#include "rate_config.h"

#include <stdatomic.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/*
 * Phase 12: clock-aware request-processing API over the Phase 11 limiter.
 *
 * Callbacks must supply milliseconds from one consistent monotonic epoch.
 * The callback and its context must be safe for simultaneous worker calls.
 * A false callback result MUST leave *now_ms unchanged (service does not
 * assume it). A successful callback must initialize *now_ms.
 *
 * Lifetime: zero-initialize the service, initialize before starting workers,
 * do not mutate clock/context while in use, join workers before destroy.
 * No clock reads, memory allocation or sleep occur inside the bucket CAS path.
 * The request path DOES call the clock provider, whose progress properties
 * are separate from those of the underlying lock-free data structures.
 */
typedef bool (*rate_service_clock_fn)(void *context, uint64_t *now_ms);

/* Phase 15: cumulative outcomes from completed initialized-service requests.
 * Each relaxed atomic increment is independent; snapshots taken during
 * concurrent requests are NOT one atomic cross-counter transaction. */
typedef struct {
    _Atomic uint64_t allowed;
    _Atomic uint64_t denied;
    _Atomic uint64_t table_full;
    _Atomic uint64_t invalid;
    _Atomic uint64_t clock_error;
} rate_service_counters_t;

typedef struct {
    ip_rate_limiter_t limiter;
    rate_service_clock_fn clock;
    void *clock_context;
    rate_service_counters_t counters;
} rate_service_t;

typedef enum {
    RATE_SERVICE_ALLOWED,
    RATE_SERVICE_DENIED,
    RATE_SERVICE_TABLE_FULL,
    RATE_SERVICE_INVALID,
    RATE_SERVICE_CLOCK_ERROR
} rate_service_result_t;

/* Phase 15: values for snapshots (without atomic members).
 * After ALL request threads join, each outcome is counted exactly once.
 * While requests are running, independent loads do not form a coherent
 * transactional snapshot. Counters wrap modulo 2^64 if exhausted. */
typedef struct {
    uint64_t allowed;
    uint64_t denied;
    uint64_t table_full;
    uint64_t invalid;
    uint64_t clock_error;
} rate_service_stats_t;

typedef enum {
    RATE_STATUS_ADMIT,
    RATE_STATUS_THROTTLED,
    RATE_STATUS_CAPACITY_EXHAUSTED,
    RATE_STATUS_BAD_REQUEST,
    RATE_STATUS_CLOCK_UNAVAILABLE,
    RATE_STATUS_UNKNOWN
} rate_service_status_t;

/* Deterministic classification; pure, allocation-free helpers. */
rate_service_status_t rate_service_classify(rate_service_result_t result);
const char *rate_service_result_name(rate_service_result_t result);

/* Available only for an initialized service. Does not reset counters.
 * On false, out remains unmodified. Initialization/destroy are quiescent-only.
 * An invalid call on a nonexistent/uninitialized service cannot be counted. */
bool rate_service_get_stats(const rate_service_t *service,
                            rate_service_stats_t *out);

/* Adapter for Linux/POSIX CLOCK_MONOTONIC; context is ignored.
 * Returns milliseconds, truncated from nanoseconds (never rounded up).
 * On failure, *now_ms is not modified when non-NULL.
 */
bool rate_service_monotonic_ms(void *context, uint64_t *now_ms);

/* service must start as {0}. Clock sampled once to seed all full buckets.
 * Returns false for invalid input, unsupported lock-free atomics, allocation
 * failure, clock error or an out-of-range clock sample. Remains empty on fail.
 */
bool rate_service_init(rate_service_t *service, size_t slots,
                       const bucket_policy_t *policy,
                       rate_service_clock_fn clock, void *clock_context);

/* Phase 13: validate resource limits before sampling the clock or allocating.
 * Calls rate_config_plan; use it directly for detailed validation diagnostics.
 * The config is copied into immutable bucket policies during initialization;
 * the caller may release or modify its input only AFTER init returns.
 * On false, an initially empty service remains empty. Legacy init() remains
 * available but does NOT enforce the new memory_budget_bytes constraint.
 */
bool rate_service_init_with_config(rate_service_t *service,
                                   const rate_config_t *config,
                                   rate_service_clock_fn clock,
                                   void *clock_context);

/* Convenience initialization using Linux CLOCK_MONOTONIC. */
bool rate_service_init_monotonic(rate_service_t *service, size_t slots,
                                 const bucket_policy_t *policy);

/* Stop/join all worker threads first. Safe for an empty/failed object. */
void rate_service_destroy(rate_service_t *service);

/* Phase 14 diagnostic: captured from the SAME successful CAS that made the
 * admission decision. Not a live, stable snapshot: other workers can update
 * the bucket by the time this function returns. The sample is truncated ms.
 * On every valid call, result is populated; has_bucket_state is true only
 * for decisions ALLOWED/DENIED. Other fields are zeroed on failures.
 * retry_possible is meaningful only for DENIED; request > capacity or a
 * required refill beyond the timestamp limit cannot have a retry hint.
 * An oversized request from an unseen IP is DENIED without slot insertion;
 * has_bucket_state remains false and no retry hint is possible.
 * retry_after_ms is a lower bound based on the sampled monotonic clock. */
typedef struct {
    rate_service_result_t result;
    bool has_bucket_state;
    uint64_t sampled_now_ms;
    uint64_t refill_anchor_ms;
    uint16_t remaining_tokens;
    uint16_t capacity;
    uint64_t refill_interval_ms;
    bool retry_possible;
    uint64_t retry_after_ms;
} rate_service_diagnostic_t;

/* Returns false only if out is NULL; in this case NO request is attempted.
 * Otherwise returns true and writes out->result for every outcome.
 * Safe for concurrent requests when normal service lifetime rules hold. */
bool rate_service_check_detailed(rate_service_t *service, uint32_t ipv4,
                                 uint32_t tokens,
                                 rate_service_diagnostic_t *out);

/* Invalid argument or clock failure MUST NOT claim a new IP slot.
 * RATE_SERVICE_DENIED means a valid request with insufficient tokens.
 * RATE_SERVICE_TABLE_FULL means a new key cannot fit; existing keys still work.
 */
rate_service_result_t rate_service_check(rate_service_t *service,
                                         uint32_t ipv4, uint32_t tokens);

#endif
