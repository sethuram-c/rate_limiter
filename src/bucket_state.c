#include "bucket_state.h"

#include <stddef.h>

static bool policy_valid(const bucket_policy_t *policy)
{
    return policy != NULL && policy->capacity != 0U &&
           policy->refill_interval_ms != 0U &&
           policy->refill_interval_ms <= BUCKET_STATE_MAX_TIME_MS;
}

bool bucket_state_pack(uint64_t anchor_ms, uint16_t tokens,
                       uint64_t *out_state)
{
    if (out_state == NULL || anchor_ms > BUCKET_STATE_MAX_TIME_MS) {
        return false;
    }
    *out_state = (anchor_ms << BUCKET_STATE_TOKEN_BITS) | (uint64_t)tokens;
    return true;
}

void bucket_state_unpack(uint64_t state, uint64_t *anchor_ms,
                         uint16_t *tokens)
{
    if (anchor_ms != NULL) {
        *anchor_ms = state >> BUCKET_STATE_TOKEN_BITS;
    }
    if (tokens != NULL) {
        *tokens = (uint16_t)(state & UINT16_MAX);
    }
}

bool bucket_state_initial(const bucket_policy_t *policy,
                          uint64_t now_ms, uint64_t *out_state)
{
    if (!policy_valid(policy) || out_state == NULL) {
        return false;
    }
    return bucket_state_pack(now_ms, policy->capacity, out_state);
}

bool bucket_state_plan(const bucket_policy_t *policy,
                       uint64_t observed_state,
                       uint32_t requested_tokens,
                       uint64_t now_ms,
                       bucket_transition_t *out)
{
    uint64_t anchor_ms;
    uint64_t elapsed_ms;
    uint64_t gained;
    uint64_t space;
    uint16_t tokens;
    bucket_transition_t candidate;

    if (!policy_valid(policy) || out == NULL || requested_tokens == 0U ||
        now_ms > BUCKET_STATE_MAX_TIME_MS) {
        return false;
    }

    bucket_state_unpack(observed_state, &anchor_ms, &tokens);
    if (tokens > policy->capacity) {
        return false;
    }

    if (now_ms > anchor_ms) {
        elapsed_ms = now_ms - anchor_ms;
        gained = elapsed_ms / policy->refill_interval_ms;
        if (gained != 0U) {
            /* gained * interval <= elapsed, so this cannot overflow. */
            anchor_ms += gained * policy->refill_interval_ms;
            space = (uint64_t)policy->capacity - (uint64_t)tokens;
            if (gained >= space) {
                tokens = policy->capacity;
            } else {
                tokens = (uint16_t)((uint64_t)tokens + gained);
            }
        }
    }

    candidate.allowed = requested_tokens <= (uint32_t)tokens;
    if (candidate.allowed) {
        tokens = (uint16_t)((uint32_t)tokens - requested_tokens);
    }

    /* anchor_ms remains <= BUCKET_STATE_MAX_TIME_MS. */
    candidate.next_state = (anchor_ms << BUCKET_STATE_TOKEN_BITS) |
                           (uint64_t)tokens;
    *out = candidate;
    return true;
}
