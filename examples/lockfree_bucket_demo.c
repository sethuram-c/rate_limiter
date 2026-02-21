#include "lockfree_bucket.h"

#include <inttypes.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

int main(void)
{
    lf_bucket_t bucket;
    bucket_policy_t policy = {.capacity = 3, .refill_interval_ms = 100};
    const uint64_t times[] = {1000, 1000, 1000, 1000, 1099, 1100};

    if (!lf_bucket_init(&bucket, &policy, 1000)) {
        fputs("initialization failed (invalid policy or non-lock-free atomic)\n",
              stderr);
        return 1;
    }
    puts("Phase 7: packed 64-bit atomic token bucket");
    for (size_t i = 0; i < sizeof(times) / sizeof(times[0]); ++i) {
        bool allowed;
        uint64_t anchor;
        uint16_t tokens;
        if (!lf_bucket_try_consume(&bucket, 1, times[i], &allowed) ||
            !lf_bucket_snapshot(&bucket, &anchor, &tokens)) {
            return 1;
        }
        printf("t=%" PRIu64 "ms  %s  tokens=%u  anchor=%" PRIu64 "\n",
               times[i], allowed ? "ALLOW" : "DENY ",
               (unsigned)tokens, anchor);
    }
    printf("state atomic lock-free: %s\n",
           atomic_is_lock_free(&bucket.state) ? "yes" : "no");
    return 0;
}
