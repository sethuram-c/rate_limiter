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

static void test_batch_consumption_is_all_or_nothing(void)
{
    token_bucket_t bucket;

    assert(token_bucket_init(&bucket, 5U, MS(100), 0U) == 0);
    assert(token_bucket_try_consume_n(&bucket, 3U, 0U));
    assert(bucket.tokens == 2U);
    assert(!token_bucket_try_consume_n(&bucket, 3U, 0U));
    assert(bucket.tokens == 2U); /* No partial deduction. */
    assert(token_bucket_try_consume(&bucket, 0U));
    assert(bucket.tokens == 1U); /* Old one-token API still works. */
}

static void test_zero_batch_does_not_mutate_state(void)
{
    token_bucket_t bucket;
    uint64_t anchor;

    assert(token_bucket_init(&bucket, 2U, MS(100), 0U) == 0);
    assert(token_bucket_try_consume_n(&bucket, 2U, 0U));
    anchor = bucket.last_refill_ns;
    assert(!token_bucket_try_consume_n(&bucket, 0U, MS(500)));
    assert(bucket.tokens == 0U && bucket.last_refill_ns == anchor);
    assert(token_bucket_available(&bucket, MS(500)) == 2U);
}

static void test_failed_batch_still_applies_refill(void)
{
    token_bucket_t bucket;

    assert(token_bucket_init(&bucket, 4U, MS(100), 0U) == 0);
    assert(token_bucket_try_consume_n(&bucket, 4U, 0U));
    assert(!token_bucket_try_consume_n(&bucket, 3U, MS(200)));
    assert(bucket.tokens == 2U);
    assert(bucket.last_refill_ns == MS(200));
}

static void test_large_intervals_and_timestamp_limits(void)
{
    token_bucket_t bucket;

    assert(token_bucket_init(&bucket, 1U, UINT64_MAX, 0U) == 0);
    assert(token_bucket_try_consume(&bucket, 0U));
    assert(token_bucket_available(&bucket, UINT64_MAX - 1U) == 0U);
    assert(token_bucket_available(&bucket, UINT64_MAX) == 1U);
    assert(bucket.last_refill_ns == UINT64_MAX);

    assert(token_bucket_init(&bucket, 4U, 10U, UINT64_MAX - 50U) == 0);
    assert(token_bucket_try_consume_n(&bucket, 4U, UINT64_MAX - 50U));
    assert(token_bucket_available(&bucket, UINT64_MAX - 1U) == 4U);
    assert(bucket.last_refill_ns == UINT64_MAX - 10U);
    assert(token_bucket_available(&bucket, UINT64_MAX) == 4U);
    assert(bucket.last_refill_ns == UINT64_MAX);
}

static void test_large_capacity_refill_never_overflows(void)
{
    token_bucket_t bucket;

    assert(token_bucket_init(&bucket, UINT64_MAX, 1U, 0U) == 0);
    assert(token_bucket_try_consume_n(&bucket, UINT64_MAX, 0U));
    assert(bucket.tokens == 0U);
    assert(!token_bucket_try_consume(&bucket, 0U));
    assert(token_bucket_available(&bucket, UINT64_MAX) == UINT64_MAX);
    assert(bucket.last_refill_ns == UINT64_MAX);
}

static void test_stale_time_does_not_rewind_anchor(void)
{
    token_bucket_t bucket;

    assert(token_bucket_init(&bucket, 2U, 10U, 100U) == 0);
    assert(token_bucket_try_consume_n(&bucket, 2U, 100U));
    assert(token_bucket_available(&bucket, 125U) == 2U);
    assert(bucket.last_refill_ns == 120U);
    assert(token_bucket_try_consume(&bucket, 111U));
    assert(bucket.last_refill_ns == 120U);
    assert(token_bucket_available(&bucket, 129U) == 1U);
    assert(token_bucket_available(&bucket, 130U) == 2U);
}

static void test_invalid_init_does_not_modify_existing_bucket(void)
{
    token_bucket_t bucket;

    assert(token_bucket_init(&bucket, 3U, MS(100), MS(7)) == 0);
    assert(token_bucket_init(&bucket, 0U, MS(100), 0U) == -1);
    assert(bucket.capacity == 3U && bucket.tokens == 3U);
    assert(bucket.refill_interval_ns == MS(100));
    assert(bucket.last_refill_ns == MS(7));
}

static void test_null_bucket_operations_are_safe(void)
{
    assert(!token_bucket_try_consume(NULL, 0U));
    assert(!token_bucket_try_consume_n(NULL, 3U, 0U));
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
    test_batch_consumption_is_all_or_nothing();
    test_zero_batch_does_not_mutate_state();
    test_failed_batch_still_applies_refill();
    test_large_intervals_and_timestamp_limits();
    test_large_capacity_refill_never_overflows();
    test_stale_time_does_not_rewind_anchor();
    test_invalid_init_does_not_modify_existing_bucket();
    test_null_bucket_operations_are_safe();

    puts("token bucket tests passed");
    return 0;
}
