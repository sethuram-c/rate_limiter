#ifndef RATE_SERVICE_H
#define RATE_SERVICE_H

#include "ip_rate_limiter.h"
#include "rate_config.h"

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

typedef struct {
    ip_rate_limiter_t limiter;
    rate_service_clock_fn clock;
    void *clock_context;
} rate_service_t;

typedef enum {
    RATE_SERVICE_ALLOWED,
    RATE_SERVICE_DENIED,
    RATE_SERVICE_TABLE_FULL,
    RATE_SERVICE_INVALID,
    RATE_SERVICE_CLOCK_ERROR
} rate_service_result_t;

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

/* Invalid argument or clock failure MUST NOT claim a new IP slot.
 * RATE_SERVICE_DENIED means a valid request with insufficient tokens.
 * RATE_SERVICE_TABLE_FULL means a new key cannot fit; existing keys still work.
 */
rate_service_result_t rate_service_check(rate_service_t *service,
                                         uint32_t ipv4, uint32_t tokens);

#endif
