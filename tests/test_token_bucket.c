#include "token_bucket.h"

#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#define MS(value) (UINT64_C(value) * UINT64_C(1000000))

static void test_initializes_full(void)
{
    token_bucket_t bucket;

    assert(token_bucket_init(&bucket, 4U, MS(100), MS(10)) == 0);
    assert(token_bucket_available(&bucket, MS(10)) == 4U);
}

static void test_rejects_invalid_configuration(void)
{
    token_bucket_t bucket;

    assert(token_bucket_init(NULL, 1U, MS(100), 0U) == -1);
    assert(token_bucket_init(&bucket, 0U, MS(100), 0U) == -1);
    assert(token_bucket_init(&bucket, 1U, 0U, 0U) == -1);
}

static void test_consumes_until_empty(void)
{
    token_bucket_t bucket;

    assert(token_bucket_init(&bucket, 2U, MS(100), 0U) == 0);
    assert(token_bucket_try_consume(&bucket, 0U));
    assert(token_bucket_try_consume(&bucket, 0U));
    assert(!token_bucket_try_consume(&bucket, 0U));
    assert(token_bucket_available(&bucket, 0U) == 0U);
}

static void test_refills_only_after_complete_interval(void)
{
    token_bucket_t bucket;

    assert(token_bucket_init(&bucket, 2U, MS(100), 0U) == 0);
    assert(token_bucket_try_consume(&bucket, 0U));
    assert(token_bucket_try_consume(&bucket, 0U));

    assert(token_bucket_available(&bucket, MS(99)) == 0U);
    assert(token_bucket_available(&bucket, MS(100)) == 1U);
    assert(token_bucket_available(&bucket, MS(199)) == 1U);
    assert(token_bucket_available(&bucket, MS(200)) == 2U);
}

static void test_preserves_partial_elapsed_time(void)
{
    token_bucket_t bucket;

    assert(token_bucket_init(&bucket, 3U, MS(100), 0U) == 0);
    assert(token_bucket_try_consume(&bucket, 0U));
    assert(token_bucket_try_consume(&bucket, 0U));
    assert(token_bucket_try_consume(&bucket, 0U));

    assert(token_bucket_available(&bucket, MS(150)) == 1U);
    assert(token_bucket_available(&bucket, MS(199)) == 1U);
    assert(token_bucket_available(&bucket, MS(200)) == 2U);
}

static void test_caps_refill_at_capacity(void)
{
    token_bucket_t bucket;

    assert(token_bucket_init(&bucket, 3U, MS(100), 0U) == 0);
    assert(token_bucket_try_consume(&bucket, 0U));

    assert(token_bucket_available(&bucket, MS(1000)) == 3U);
}

static void test_older_timestamp_does_not_refill(void)
{
    token_bucket_t bucket;

    assert(token_bucket_init(&bucket, 1U, MS(100), MS(500)) == 0);
    assert(token_bucket_try_consume(&bucket, MS(500)));
    assert(!token_bucket_try_consume(&bucket, MS(400)));
    assert(token_bucket_available(&bucket, MS(400)) == 0U);
}

static void test_null_bucket_operations_are_safe(void)
{
    assert(!token_bucket_try_consume(NULL, 0U));
    assert(token_bucket_available(NULL, 0U) == 0U);
}

int main(void)
{
    test_initializes_full();
    test_rejects_invalid_configuration();
    test_consumes_until_empty();
    test_refills_only_after_complete_interval();
    test_preserves_partial_elapsed_time();
    test_caps_refill_at_capacity();
    test_older_timestamp_does_not_refill();
    test_null_bucket_operations_are_safe();

    puts("token bucket tests passed");
    return 0;
}
