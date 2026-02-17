#include "monotonic_clock.h"
#include "token_bucket.h"

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define NS_PER_MS UINT64_C(1000000)

static int run_deterministic_demo(void)
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

static int run_live_demo(void)
{
    token_bucket_t bucket;
    uint64_t now_ns;
    const char *decision;

    if (monotonic_clock_now_ns(&now_ns) != 0) {
        perror("clock_gettime(CLOCK_MONOTONIC)");
        return EXIT_FAILURE;
    }

    if (token_bucket_init(&bucket, 3U, 100U * NS_PER_MS, now_ns) != 0) {
        fputs("failed to initialize token bucket\n", stderr);
        return EXIT_FAILURE;
    }

    decision = token_bucket_try_consume(&bucket, now_ns) ? "ALLOW" : "DENY";
    printf("mode=live clock=CLOCK_MONOTONIC monotonic_ns=%" PRIu64 "\n", now_ns);
    printf("live request=1 decision=%s tokens=%" PRIu64 "\n",
           decision, token_bucket_available(&bucket, now_ns));
    return EXIT_SUCCESS;
}

int main(int argc, char **argv)
{
    if (argc == 1) {
        return run_deterministic_demo();
    }
    if (argc == 2 && strcmp(argv[1], "--live") == 0) {
        return run_live_demo();
    }
    if (argc == 2 && strcmp(argv[1], "--help") == 0) {
        puts("Usage: ./build/ratelimiter [--live|--help]");
        puts("  default: deterministic timestamps for repeatable output");
        puts("  --live:  sample Linux CLOCK_MONOTONIC");
        return EXIT_SUCCESS;
    }

    fputs("Usage: ./build/ratelimiter [--live|--help]\n", stderr);
    return EXIT_FAILURE;
}
