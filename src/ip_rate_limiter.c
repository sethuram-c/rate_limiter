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
