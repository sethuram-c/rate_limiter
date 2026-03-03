#ifndef MUTEX_RATE_LIMITER_H
#define MUTEX_RATE_LIMITER_H

#include "ip_rate_limiter.h"  /* identical public admission-result categories */

#include <pthread.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Phase 20 comparison baseline, NOT a lock-free implementation.
 * The concurrent IP index is deliberately shared with the lock-free design.
 * Only per-IP bucket-state synchronization changes: pthread_mutex_t vs CAS.
 * This controlled hybrid baseline isolates bucket synchronization costs;
 * it is NOT a comparison of two completely different map designs.
 *
 * Initialize while quiescent; keep the object alive for all callers; join
 * worker threads before destruction. All slots are preinitialized before
 * the atomic index publishes its first key. Never copy a live mutex object.
 */
typedef struct {
    pthread_mutex_t mutex;
    uint64_t state;  /* plain packed state; protected by mutex */
} mutex_bucket_t;

typedef struct {
    ip_atomic_table_t index;
    mutex_bucket_t *buckets;
    bucket_policy_t policy; /* immutable while workers execute */
} mutex_rate_limiter_t;

/* Starts each IP slot with a FULL bucket; initialize limiter as {0}. */
bool mutex_rate_limiter_init(mutex_rate_limiter_t *limiter, size_t slots,
                             const bucket_policy_t *policy, uint64_t now_ms);

/* Quiescent-only: worker threads must have stopped and joined first. */
void mutex_rate_limiter_destroy(mutex_rate_limiter_t *limiter);

/* Same public return categories and timestamp/amount contract as the
 * lock-free ip_rate_limiter_check(). pthread lock errors return INVALID. */
ip_rate_decision_t mutex_rate_limiter_check(mutex_rate_limiter_t *limiter,
                                            uint32_t ip, uint32_t amount,
                                            uint64_t now_ms);

/* For deterministic equivalence tests; no lock is retained after return. */
bool mutex_rate_limiter_snapshot(mutex_rate_limiter_t *limiter, uint32_t ip,
                                 bool *found, uint64_t *anchor_ms,
                                 uint16_t *tokens);
#endif
