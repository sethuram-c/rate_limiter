#ifndef IP_RATE_LIMITER_H
#define IP_RATE_LIMITER_H

#include "ip_atomic_table.h"
#include "lockfree_bucket.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/*
 * Phase 11: a fixed-capacity, concurrent, per-IPv4 token bucket limiter.
 *
 * The whole array of buckets is initialized BEFORE any key may be published
 * or any worker may use the object. A successful atomic key insertion selects
 * a pre-existing, fully initialized bucket by stable slot index.
 *
 * The caller must: initialize while quiescent, never reinitialize or resize
 * while in use, pass timestamps in milliseconds from one monotonic clock,
 * and join all worker threads BEFORE destruction.
 *
 * No allocation, mutex or reclamation is performed on the request path.
 * This guarantees no blocking synchronization only on platforms where BOTH
 * underlying 64-bit atomic types pass their lock-free capability checks.
 * A specific request is NOT wait-free: probing and CAS may face contention.
 */
typedef struct {
    ip_atomic_table_t index;
    lf_bucket_t *buckets;
} ip_rate_limiter_t;

typedef enum {
    IP_RATE_ALLOWED,
    IP_RATE_DENIED,
    IP_RATE_TABLE_FULL,
    IP_RATE_INVALID
} ip_rate_decision_t;

/* limiter must start as {0}. All buckets start full with the same policy. */
bool ip_rate_limiter_init(ip_rate_limiter_t *limiter, size_t slots,
                          const bucket_policy_t *policy, uint64_t now_ms);

/* Quiescent-only destruction: caller must stop/join all users first. */
void ip_rate_limiter_destroy(ip_rate_limiter_t *limiter);

/* No mutation for invalid inputs; FULL does not consume another IP's tokens. */
ip_rate_decision_t ip_rate_limiter_check(ip_rate_limiter_t *limiter,
                                         uint32_t ip, uint32_t amount,
                                         uint64_t now_ms);

/* Phase 14: observation captured by the request's own CAS.
 * valid_state is true ONLY for ALLOWED/DENIED, never for FULL/INVALID.
 * Fields are copied before returning; they describe the successful CAS
 * and can immediately be superseded by other concurrent operations. */
typedef struct {
    ip_rate_decision_t decision;
    bool valid_state;
    uint64_t committed_state;
    bucket_policy_t policy;
} ip_rate_report_t;

/* On non-NULL out, reports any decision, including INVALID/FULL.
 * Returns false only if out is NULL; then no request is attempted. */
bool ip_rate_limiter_check_report(ip_rate_limiter_t *limiter,
                                  uint32_t ip, uint32_t amount,
                                  uint64_t now_ms, ip_rate_report_t *out);

/* For observation only: NOT a transactional snapshot across different IPs.
 * On success, *found says if ip is visible; if !found, other outputs unchanged.
 * The key can be inserted immediately after a NOT FOUND response.
 */
bool ip_rate_limiter_snapshot(const ip_rate_limiter_t *limiter,
                              uint32_t ip, bool *found,
                              uint64_t *anchor_ms, uint16_t *tokens);

#endif
