#include "lockfree_bucket.h"

#include <stddef.h>

bool lf_bucket_init(lf_bucket_t *bucket, const bucket_policy_t *policy,
                    uint64_t initial_now_ms)
{
    uint64_t initial;

    if (bucket == NULL || policy == NULL ||
        !bucket_state_initial(policy, initial_now_ms, &initial)) {
        return false;
    }

    bucket->policy = *policy;
    atomic_init(&bucket->state, initial);
    /* Lock-free progress depends on the implementation and platform. */
    return atomic_is_lock_free(&bucket->state);
}

bool lf_bucket_try_consume(lf_bucket_t *bucket, uint32_t amount,
                           uint64_t now_ms, bool *allowed)
{
    uint64_t observed;

    if (bucket == NULL || allowed == NULL || amount == 0U ||
        now_ms > BUCKET_STATE_MAX_TIME_MS) {
        return false;
    }

    observed = atomic_load_explicit(&bucket->state, memory_order_relaxed);
    for (;;) {
        bucket_transition_t candidate;

        if (!bucket_state_plan(&bucket->policy, observed, amount, now_ms,
                               &candidate)) {
            return false;
        }

        /*
         * On failure, C11 rewrites observed with the current state.
         * Never use a stale candidate after losing the CAS race.
         * Relaxed ordering suffices: the entire mutable logical state is
         * this one atomic object; policy is immutable and pre-published.
         */
        if (atomic_compare_exchange_weak_explicit(
                &bucket->state, &observed, candidate.next_state,
                memory_order_relaxed, memory_order_relaxed)) {
            *allowed = candidate.allowed;
            return true;
        }
    }
}

bool lf_bucket_snapshot(const lf_bucket_t *bucket, uint64_t *anchor_ms,
                        uint16_t *tokens)
{
    uint64_t packed;

    if (bucket == NULL || anchor_ms == NULL || tokens == NULL) {
        return false;
    }
    packed = atomic_load_explicit(&bucket->state, memory_order_relaxed);
    bucket_state_unpack(packed, anchor_ms, tokens);
    return true;
}
