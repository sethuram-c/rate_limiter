#include "ip_rate_limiter.h"

#include <stdint.h>
#include <stdlib.h>

static bool ready(const ip_rate_limiter_t *limiter)
{
    return limiter != NULL && limiter->buckets != NULL &&
           limiter->index.slots != NULL && limiter->index.capacity != 0U;
}

bool ip_rate_limiter_init(ip_rate_limiter_t *limiter, size_t slots,
                          const bucket_policy_t *policy, uint64_t now_ms)
{
    lf_bucket_t *buckets;
    size_t i;

    if (limiter == NULL || limiter->buckets != NULL ||
        limiter->index.slots != NULL || limiter->index.capacity != 0U ||
        policy == NULL || now_ms > BUCKET_STATE_MAX_TIME_MS ||
        slots > SIZE_MAX / sizeof(lf_bucket_t)) {
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
    /* Publication cannot occur yet: initialized slots have no visible keys. */
    for (i = 0U; i < slots; ++i) {
        if (!lf_bucket_init(&buckets[i], policy, now_ms)) {
            free(buckets);
            ip_atomic_table_destroy(&limiter->index);
            return false;
        }
    }
    limiter->buckets = buckets;
    return true;
}

void ip_rate_limiter_destroy(ip_rate_limiter_t *limiter)
{
    if (limiter != NULL) {
        free(limiter->buckets);
        limiter->buckets = NULL;
        ip_atomic_table_destroy(&limiter->index);
    }
}

ip_rate_decision_t ip_rate_limiter_check(ip_rate_limiter_t *limiter,
                                         uint32_t ip, uint32_t amount,
                                         uint64_t now_ms)
{
    ip_atomic_insert_result_t slot;
    bool allowed;

    /* Validate BEFORE claiming an IP slot (a failed request has no effect). */
    if (!ready(limiter) || amount == 0U ||
        now_ms > BUCKET_STATE_MAX_TIME_MS) {
        return IP_RATE_INVALID;
    }
    if (!ip_atomic_table_get_or_insert(&limiter->index, ip, &slot)) {
        return IP_RATE_INVALID;
    }
    if (slot.kind == IP_ATOMIC_FULL) {
        return IP_RATE_TABLE_FULL;
    }
    if (!lf_bucket_try_consume(&limiter->buckets[slot.index], amount,
                               now_ms, &allowed)) {
        return IP_RATE_INVALID;
    }
    return allowed ? IP_RATE_ALLOWED : IP_RATE_DENIED;
}

bool ip_rate_limiter_snapshot(const ip_rate_limiter_t *limiter,
                              uint32_t ip, bool *found,
                              uint64_t *anchor_ms, uint16_t *tokens)
{
    bool visible;
    size_t index;

    if (!ready(limiter) || found == NULL || anchor_ms == NULL ||
        tokens == NULL ||
        !ip_atomic_table_find(&limiter->index, ip, &visible, &index)) {
        return false;
    }
    if (visible &&
        !lf_bucket_snapshot(&limiter->buckets[index], anchor_ms, tokens)) {
        return false;
    }
    *found = visible;
    return true;
}
