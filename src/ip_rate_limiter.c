#include "ip_rate_limiter.h"

#include <stdint.h>


static bool ready(const ip_rate_limiter_t *limiter)
{
    return limiter != NULL && limiter->buckets != NULL &&
           limiter->index.slots != NULL && limiter->index.capacity != 0U;
}

bool ip_rate_limiter_init_with_allocator(ip_rate_limiter_t *limiter,
                                         size_t slots,
                                         const bucket_policy_t *policy,
                                         uint64_t now_ms,
                                         const rl_allocator_t *allocator)
{
    lf_bucket_t *buckets;
    size_t i;

    if (limiter == NULL || limiter->buckets != NULL ||
        limiter->index.slots != NULL || limiter->index.capacity != 0U ||
        limiter->allocator.allocate != NULL ||
        policy == NULL || now_ms > BUCKET_STATE_MAX_TIME_MS ||
        slots > SIZE_MAX / sizeof(lf_bucket_t) ||
        !rl_allocator_valid(allocator)) {
        return false;
    }
    if (!ip_atomic_table_init_with_allocator(&limiter->index, slots,
                                             allocator)) {
        return false;
    }
    /* The same allocator handles compact and 64-byte-padded layouts. */
    buckets = rl_allocator_alloc(allocator, _Alignof(lf_bucket_t),
                                 slots * sizeof(*buckets));
    if (buckets == NULL) {
        ip_atomic_table_destroy(&limiter->index);
        return false;
    }
    /* Publication cannot occur yet: initialized slots have no visible keys. */
    for (i = 0U; i < slots; ++i) {
        if (!lf_bucket_init(&buckets[i], policy, now_ms)) {
            rl_allocator_free(allocator, buckets);
            ip_atomic_table_destroy(&limiter->index);
            return false;
        }
    }
    limiter->buckets = buckets;
    if (allocator != NULL) {
        limiter->allocator = *allocator;
    }
    return true;
}

bool ip_rate_limiter_init(ip_rate_limiter_t *limiter, size_t slots,
                          const bucket_policy_t *policy, uint64_t now_ms)
{
    return ip_rate_limiter_init_with_allocator(limiter, slots, policy,
                                               now_ms, NULL);
}

void ip_rate_limiter_destroy(ip_rate_limiter_t *limiter)
{
    if (limiter != NULL) {
        rl_allocator_free(limiter->allocator.allocate != NULL
                          ? &limiter->allocator : NULL, limiter->buckets);
        limiter->buckets = NULL;
        limiter->allocator = (rl_allocator_t){0};
        ip_atomic_table_destroy(&limiter->index);
    }
}

bool ip_rate_limiter_check_report(ip_rate_limiter_t *limiter,
                                  uint32_t ip, uint32_t amount,
                                  uint64_t now_ms, ip_rate_report_t *out)
{
    ip_atomic_insert_result_t slot;
    ip_rate_report_t report = {0};
    bool allowed;
    uint64_t committed;

    if (out == NULL) {
        return false;
    }
    report.decision = IP_RATE_INVALID;
    /* Validate BEFORE claiming an IP slot (a failed request has no effect). */
    if (!ready(limiter) || amount == 0U ||
        now_ms > BUCKET_STATE_MAX_TIME_MS ||
        !ip_atomic_table_get_or_insert(&limiter->index, ip, &slot)) {
        *out = report;
        return true;
    }
    if (slot.kind == IP_ATOMIC_FULL) {
        report.decision = IP_RATE_TABLE_FULL;
        *out = report;
        return true;
    }
    if (!lf_bucket_try_consume_observed(&limiter->buckets[slot.index],
                                        amount, now_ms, &allowed,
                                        &committed)) {
        *out = report;
        return true;
    }
    report.decision = allowed ? IP_RATE_ALLOWED : IP_RATE_DENIED;
    report.valid_state = true;
    report.committed_state = committed;
    report.policy = limiter->buckets[slot.index].policy;
    *out = report;
    return true;
}

ip_rate_decision_t ip_rate_limiter_check(ip_rate_limiter_t *limiter,
                                         uint32_t ip, uint32_t amount,
                                         uint64_t now_ms)
{
    ip_rate_report_t report;
    (void)ip_rate_limiter_check_report(limiter, ip, amount, now_ms, &report);
    return report.decision;
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
