#include "mutex_rate_limiter.h"

#include <stdint.h>
#include <stdlib.h>

static bool ready(const mutex_rate_limiter_t *limiter)
{
    return limiter != NULL && limiter->buckets != NULL &&
           limiter->index.slots != NULL && limiter->index.capacity != 0U;
}

bool mutex_rate_limiter_init(mutex_rate_limiter_t *limiter, size_t slots,
                             const bucket_policy_t *policy, uint64_t now_ms)
{
    uint64_t initial_state;
    mutex_bucket_t *buckets;
    size_t i;

    if (limiter == NULL || limiter->buckets != NULL ||
        limiter->index.slots != NULL || limiter->index.capacity != 0U ||
        policy == NULL || slots > SIZE_MAX / sizeof(mutex_bucket_t) ||
        !bucket_state_initial(policy, now_ms, &initial_state)) {
        return false;
    }
    if (!ip_atomic_table_init(&limiter->index, slots)) {
        return false;
    }
    buckets = malloc(slots * sizeof(*buckets));
    if (buckets == NULL) {
        ip_atomic_table_destroy(&limiter->index);
        return false;
    }
    for (i = 0U; i < slots; ++i) {
        if (pthread_mutex_init(&buckets[i].mutex, NULL) != 0) {
            while (i > 0U) {
                --i;
                (void)pthread_mutex_destroy(&buckets[i].mutex);
            }
            free(buckets);
            ip_atomic_table_destroy(&limiter->index);
            return false;
        }
        buckets[i].state = initial_state;
    }
    limiter->policy = *policy;
    limiter->buckets = buckets; /* quiescent publication, before any worker */
    return true;
}

void mutex_rate_limiter_destroy(mutex_rate_limiter_t *limiter)
{
    size_t i;
    if (limiter == NULL) {
        return;
    }
    if (limiter->buckets != NULL) {
        for (i = 0U; i < limiter->index.capacity; ++i) {
            (void)pthread_mutex_destroy(&limiter->buckets[i].mutex);
        }
        free(limiter->buckets);
        limiter->buckets = NULL;
    }
    ip_atomic_table_destroy(&limiter->index);
}

ip_rate_decision_t mutex_rate_limiter_check(mutex_rate_limiter_t *limiter,
                                            uint32_t ip, uint32_t amount,
                                            uint64_t now_ms)
{
    ip_atomic_insert_result_t slot;
    bucket_transition_t transition;
    mutex_bucket_t *bucket;
    ip_rate_decision_t result;

    if (!ready(limiter) || amount == 0U ||
        now_ms > BUCKET_STATE_MAX_TIME_MS ||
        !ip_atomic_table_get_or_insert(&limiter->index, ip, &slot)) {
        return IP_RATE_INVALID;
    }
    if (slot.kind == IP_ATOMIC_FULL) {
        return IP_RATE_TABLE_FULL;
    }
    bucket = &limiter->buckets[slot.index];
    if (pthread_mutex_lock(&bucket->mutex) != 0) {
        return IP_RATE_INVALID;
    }
    if (!bucket_state_plan(&limiter->policy, bucket->state, amount,
                           now_ms, &transition)) {
        (void)pthread_mutex_unlock(&bucket->mutex);
        return IP_RATE_INVALID;
    }
    bucket->state = transition.next_state;
    result = transition.allowed ? IP_RATE_ALLOWED : IP_RATE_DENIED;
    (void)pthread_mutex_unlock(&bucket->mutex);
    return result;
}

bool mutex_rate_limiter_snapshot(mutex_rate_limiter_t *limiter, uint32_t ip,
                                 bool *found, uint64_t *anchor_ms,
                                 uint16_t *tokens)
{
    bool visible;
    size_t index;
    mutex_bucket_t *bucket;
    uint64_t packed;
    if (!ready(limiter) || found == NULL || anchor_ms == NULL ||
        tokens == NULL ||
        !ip_atomic_table_find(&limiter->index, ip, &visible, &index)) {
        return false;
    }
    if (visible) {
        bucket = &limiter->buckets[index];
        if (pthread_mutex_lock(&bucket->mutex) != 0) {
            return false;
        }
        packed = bucket->state;
        (void)pthread_mutex_unlock(&bucket->mutex);
        bucket_state_unpack(packed, anchor_ms, tokens);
    }
    *found = visible;
    return true;
}
