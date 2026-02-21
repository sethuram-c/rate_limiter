#ifndef LOCKFREE_BUCKET_H
#define LOCKFREE_BUCKET_H

#include "bucket_state.h"

#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>

/*
 * Phase 7: single-instance, fixed-policy, lock-free token bucket.
 *
 * Thread/lifetime contract:
 * - Initialize before publishing the object to any worker thread.
 * - Do not modify policy or reinitialize while workers are active.
 * - Keep the object alive until every user has finished.
 * - Pass monotonic timestamps in milliseconds from one clock epoch.
 * - If lf_bucket_init() returns false, the object is unusable.
 *
 * The single 64-bit atomic is checked with atomic_is_lock_free() at init.
 * A failed lock-free capability check means initialization fails.
 * This module does not allocate memory, spin on a mutex, or call the clock.
 */
typedef struct {
    bucket_policy_t policy;  /* immutable after successful init */
    _Atomic uint64_t state;  /* 48-bit anchor_ms | 16-bit tokens */
} lf_bucket_t;

/* Initialize a FULL bucket. Returns false for bad input or non-lock-free target. */
bool lf_bucket_init(lf_bucket_t *bucket, const bucket_policy_t *policy,
                    uint64_t initial_now_ms);

/*
 * Returns true when the operation was executed successfully.
 * On true, *allowed says whether the full request was admitted.
 * On false, *allowed is untouched. No partial consumption occurs.
 * The successful atomic compare-exchange is the linearization point.
 */
bool lf_bucket_try_consume(lf_bucket_t *bucket, uint32_t amount,
                           uint64_t now_ms, bool *allowed);

/* A coherent observation of both fields; no synchronization of other data. */
bool lf_bucket_snapshot(const lf_bucket_t *bucket, uint64_t *anchor_ms,
                        uint16_t *tokens);

#endif
