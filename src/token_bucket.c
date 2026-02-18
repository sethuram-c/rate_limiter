#include "token_bucket.h"

#include <stddef.h>

static void token_bucket_refill(token_bucket_t *bucket, uint64_t now_ns)
{
    uint64_t elapsed_ns;
    uint64_t refill_tokens;
    uint64_t space;

    /* Never rewind the refill anchor for stale or equal timestamps. */
    if (now_ns <= bucket->last_refill_ns) {
        return;
    }

    elapsed_ns = now_ns - bucket->last_refill_ns;
    refill_tokens = elapsed_ns / bucket->refill_interval_ns;

    if (refill_tokens == 0U) {
        return;
    }

    /*
     * Overflow proof: refill_tokens * interval <= elapsed_ns, and
     * last_refill_ns + elapsed_ns == now_ns <= UINT64_MAX.
     * Advance by whole intervals to keep fractional elapsed time.
     */
    bucket->last_refill_ns += refill_tokens * bucket->refill_interval_ns;

    /* Saturate by subtracting first, never by adding past capacity. */
    space = bucket->capacity - bucket->tokens;
    if (refill_tokens >= space) {
        bucket->tokens = bucket->capacity;
    } else {
        bucket->tokens += refill_tokens;
    }
}

int token_bucket_init(token_bucket_t *bucket,
                      uint64_t capacity,
                      uint64_t refill_interval_ns,
                      uint64_t now_ns)
{
    if (bucket == NULL || capacity == 0U || refill_interval_ns == 0U) {
        return -1;
    }

    bucket->capacity = capacity;
    bucket->tokens = capacity;
    bucket->refill_interval_ns = refill_interval_ns;
    bucket->last_refill_ns = now_ns;
    return 0;
}

bool token_bucket_try_consume_n(token_bucket_t *bucket,
                                uint64_t amount,
                                uint64_t now_ns)
{
    if (bucket == NULL || amount == 0U) {
        return false;
    }

    token_bucket_refill(bucket, now_ns);
    if (amount > bucket->tokens) {
        return false;
    }

    bucket->tokens -= amount;
    return true;
}

bool token_bucket_try_consume(token_bucket_t *bucket, uint64_t now_ns)
{
    return token_bucket_try_consume_n(bucket, 1U, now_ns);
}

uint64_t token_bucket_available(token_bucket_t *bucket, uint64_t now_ns)
{
    if (bucket == NULL) {
        return 0U;
    }

    token_bucket_refill(bucket, now_ns);
    return bucket->tokens;
}
