#include "token_bucket.h"

#include <stddef.h>

static void token_bucket_refill(token_bucket_t *bucket, uint64_t now_ns)
{
    uint64_t elapsed_ns;
    uint64_t refill_tokens;
    uint64_t space;

    if (now_ns <= bucket->last_refill_ns) {
        return;
    }

    elapsed_ns = now_ns - bucket->last_refill_ns;
    refill_tokens = elapsed_ns / bucket->refill_interval_ns;

    if (refill_tokens == 0U) {
        return;
    }

    /* Advance only by complete refill intervals; preserve partial elapsed time. */
    bucket->last_refill_ns += refill_tokens * bucket->refill_interval_ns;

    space = bucket->capacity - bucket->tokens;
    if (refill_tokens >= space) {
        bucket->tokens = bucket->capacity;
        return;
    }

    bucket->tokens += refill_tokens;
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

bool token_bucket_try_consume(token_bucket_t *bucket, uint64_t now_ns)
{
    if (bucket == NULL) {
        return false;
    }

    token_bucket_refill(bucket, now_ns);

    if (bucket->tokens == 0U) {
        return false;
    }

    bucket->tokens--;
    return true;
}

uint64_t token_bucket_available(token_bucket_t *bucket, uint64_t now_ns)
{
    if (bucket == NULL) {
        return 0U;
    }

    token_bucket_refill(bucket, now_ns);
    return bucket->tokens;
}
