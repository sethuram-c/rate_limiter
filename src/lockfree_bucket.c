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

static bool try_consume_impl(lf_bucket_t *bucket, uint32_t amount,
                             uint64_t now_ms, bool *allowed,
                             uint64_t *cas_attempts)
{
    uint64_t observed;
    uint64_t attempts = 0U;

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

        /* Every failed CAS refreshes observed and invalidates the old plan. */
        ++attempts;
        if (atomic_compare_exchange_weak_explicit(
                &bucket->state, &observed, candidate.next_state,
                memory_order_relaxed, memory_order_relaxed)) {
            *allowed = candidate.allowed;
            if (cas_attempts != NULL) {
                *cas_attempts = attempts;
            }
            return true;
        }
    }
}

bool lf_bucket_try_consume(lf_bucket_t *bucket, uint32_t amount,
                           uint64_t now_ms, bool *allowed)
{
    return try_consume_impl(bucket, amount, now_ms, allowed, NULL);
}

bool lf_bucket_try_consume_profiled(lf_bucket_t *bucket, uint32_t amount,
                                    uint64_t now_ms, bool *allowed,
                                    uint64_t *cas_attempts)
{
    if (cas_attempts == NULL) {
        return false;
    }
    return try_consume_impl(bucket, amount, now_ms, allowed, cas_attempts);
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
