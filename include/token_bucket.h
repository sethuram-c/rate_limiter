#ifndef TOKEN_BUCKET_H
#define TOKEN_BUCKET_H

#include <stdbool.h>
#include <stdint.h>

typedef struct {
    uint64_t capacity;
    uint64_t tokens;
    uint64_t refill_interval_ns;
    uint64_t last_refill_ns;
} token_bucket_t;

/*
 * Initialize a token bucket that starts full.
 * One token is replenished for each complete refill_interval_ns that elapses.
 * Returns 0 on success and -1 for invalid arguments.
 */
int token_bucket_init(token_bucket_t *bucket,
                      uint64_t capacity,
                      uint64_t refill_interval_ns,
                      uint64_t now_ns);

/*
 * Attempt to consume one token at now_ns.
 * Returns true when the request is admitted, false when it is rate-limited.
 */
bool token_bucket_try_consume(token_bucket_t *bucket, uint64_t now_ns);

/*
 * Refill the bucket through now_ns and return its current token count.
 * This is useful for diagnostics and deterministic tests.
 */
uint64_t token_bucket_available(token_bucket_t *bucket, uint64_t now_ns);

#endif
