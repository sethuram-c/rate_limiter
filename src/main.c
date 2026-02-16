#include "token_bucket.h"

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>

#define NS_PER_MS UINT64_C(1000000)

int main(void)
{
    token_bucket_t bucket;
    uint64_t now_ns = 0U;
    unsigned int request;

    if (token_bucket_init(&bucket, 3U, 100U * NS_PER_MS, now_ns) != 0) {
        fputs("failed to initialize token bucket\n", stderr);
        return EXIT_FAILURE;
    }

    puts("token bucket demo: capacity=3, refill=1 token/100ms");

    for (request = 1U; request <= 5U; ++request) {
        const char *decision = token_bucket_try_consume(&bucket, now_ns)
                                   ? "ALLOW"
                                   : "DENY";

        printf("t=%" PRIu64 "ms request=%u decision=%s tokens=%" PRIu64 "\n",
               now_ns / NS_PER_MS,
               request,
               decision,
               token_bucket_available(&bucket, now_ns));
    }

    now_ns = 250U * NS_PER_MS;
    printf("t=%" PRIu64 "ms after refill tokens=%" PRIu64 "\n",
           now_ns / NS_PER_MS,
           token_bucket_available(&bucket, now_ns));

    return EXIT_SUCCESS;
}
