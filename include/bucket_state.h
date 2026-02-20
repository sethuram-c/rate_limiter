#ifndef BUCKET_STATE_H
#define BUCKET_STATE_H

#include <stdbool.h>
#include <stdint.h>

/*
 * Phase 6 DESIGN MODEL, not a concurrent rate limiter.
 *
 * A future atomic_uint_fast64_t can hold the complete packed state:
 *   [48-bit monotonic timestamp in milliseconds | 16-bit token balance]
 * Together the fields can be replaced by ONE successful compare-exchange.
 *
 * Model limits (deliberate and explicit): capacity <= 65535 tokens;
 * timestamp <= 2^48-1 milliseconds; millisecond granularity.
 * This model uses plain uint64_t values: callers must NOT share a mutable
 * state slot among threads without synchronization.
 */
#define BUCKET_STATE_TOKEN_BITS 16U
#define BUCKET_STATE_MAX_TIME_MS (UINT64_MAX >> BUCKET_STATE_TOKEN_BITS)

typedef struct {
    uint16_t capacity;          /* 1 ... UINT16_MAX */
    uint64_t refill_interval_ms;/* 1 ... BUCKET_STATE_MAX_TIME_MS */
} bucket_policy_t;

typedef struct {
    uint64_t next_state; /* New packed state; commit it atomically in Phase 7. */
    bool allowed;        /* True only when all requested tokens were available. */
} bucket_transition_t;

/* On error, output arguments are unchanged. */
bool bucket_state_pack(uint64_t anchor_ms, uint16_t tokens,
                       uint64_t *out_state);
void bucket_state_unpack(uint64_t state, uint64_t *anchor_ms,
                         uint16_t *tokens);
bool bucket_state_initial(const bucket_policy_t *policy,
                          uint64_t now_ms, uint64_t *out_state);

/*
 * Pure, side-effect-free transition planner.
 * Returns false for invalid parameters or an inconsistent packed state.
 * A well-formed but insufficient request returns true with allowed=false.
 * Failed requests still accrue elapsed refill time (like Phase 4).
 * No CAS or thread-safety is claimed by this function.
 */
bool bucket_state_plan(const bucket_policy_t *policy,
                       uint64_t observed_state,
                       uint32_t requested_tokens,
                       uint64_t now_ms,
                       bucket_transition_t *out);

#endif
