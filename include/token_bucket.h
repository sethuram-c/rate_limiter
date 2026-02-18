#ifndef TOKEN_BUCKET_H
#define TOKEN_BUCKET_H

#include <stdbool.h>
#include <stdint.h>

/*
 * Sequential token bucket. Do not share a bucket across threads without
 * synchronization. All timestamps must use the same monotonic clock origin.
 *
 * Invariants after initialization and every public operation:
 *   capacity > 0; refill_interval_ns > 0; 0 <= tokens <= capacity.
 *   last_refill_ns never decreases, even if a stale timestamp is supplied.
 * An initialized bucket may be inspected, but its fields must not be changed
 * directly by callers.
 */
typedef struct {
    uint64_t capacity;
    uint64_t tokens;
    uint64_t refill_interval_ns;
    uint64_t last_refill_ns;
} token_bucket_t;

/*
 * Initialize a bucket that starts full. One token is replenished after each
 * complete refill interval. Returns 0 on success or -1 for invalid arguments.
 */
int token_bucket_init(token_bucket_t *bucket,
                      uint64_t capacity,
                      uint64_t refill_interval_ns,
                      uint64_t now_ns);

/*
 * All-or-nothing consumption of amount tokens at now_ns.
 * A zero amount is invalid and leaves the bucket unchanged. If the request
 * exceeds the available balance, it is rejected without deducting tokens;
 * elapsed-time refill still occurs. NULL returns false.
 * Not thread-safe: a future phase will introduce synchronized state.
 */
bool token_bucket_try_consume_n(token_bucket_t *bucket,
                                uint64_t amount,
                                uint64_t now_ns);

/* Convenience wrapper for one token, retaining the Phase 2 public API. */
bool token_bucket_try_consume(token_bucket_t *bucket, uint64_t now_ns);

/* Refill through now_ns and return the current token count; NULL returns 0. */
uint64_t token_bucket_available(token_bucket_t *bucket, uint64_t now_ns);

#endif
