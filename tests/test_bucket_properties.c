#include "token_bucket.h"

#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

/* Deterministic pseudo-random input: same seed produces the same test run. */
static uint32_t next_random(uint32_t *state)
{
    *state ^= *state << 13;
    *state ^= *state >> 17;
    *state ^= *state << 5;
    return *state;
}

/*
 * An intentionally simple independent reference model. Unlike production,
 * this adds one refill interval at a time. This is slow but easy to audit.
 * Test-generated timestamps are small, so anchor + interval cannot overflow.
 */
typedef struct {
    uint64_t capacity;
    uint64_t tokens;
    uint64_t interval;
    uint64_t anchor;
} reference_bucket_t;

static void reference_refill(reference_bucket_t *model, uint64_t now_ns)
{
    while (now_ns >= model->anchor &&
           now_ns - model->anchor >= model->interval) {
        model->anchor += model->interval;
        if (model->tokens < model->capacity) {
            ++model->tokens;
        }
    }
}

static bool reference_consume(reference_bucket_t *model,
                              uint64_t amount,
                              uint64_t now_ns)
{
    if (amount == 0U) {
        return false;
    }
    reference_refill(model, now_ns);
    if (amount > model->tokens) {
        return false;
    }
    model->tokens -= amount;
    return true;
}

static void test_differential_against_reference_model(void)
{
    uint32_t seed = UINT32_C(0x9E3779B9);
    unsigned int trial;

    for (trial = 0U; trial < 12U; ++trial) {
        uint64_t capacity = 1U + next_random(&seed) % 12U;
        uint64_t interval = 1U + next_random(&seed) % 31U;
        token_bucket_t actual;
        reference_bucket_t expected = { capacity, capacity, interval, 0U };
        uint64_t simulated_time = 0U;
        unsigned int iteration;

        assert(token_bucket_init(&actual, capacity, interval, 0U) == 0);
        for (iteration = 0U; iteration < 4000U; ++iteration) {
            uint64_t supplied_time;
            uint64_t amount;
            uint32_t operation;
            uint64_t old_anchor = actual.last_refill_ns;

            simulated_time += next_random(&seed) % 17U;
            supplied_time = simulated_time;
            if ((next_random(&seed) % 13U) == 0U &&
                simulated_time > 20U) {
                supplied_time -= 20U; /* Occasionally stale timestamps. */
            }
            amount = next_random(&seed) % (capacity + 3U);
            operation = next_random(&seed) % 3U;

            if (operation == 0U) {
                bool actual_ok = token_bucket_try_consume_n(
                    &actual, amount, supplied_time);
                bool expected_ok = reference_consume(
                    &expected, amount, supplied_time);
                assert(actual_ok == expected_ok);
            } else if (operation == 1U) {
                bool actual_ok = token_bucket_try_consume(
                    &actual, supplied_time);
                bool expected_ok = reference_consume(
                    &expected, 1U, supplied_time);
                assert(actual_ok == expected_ok);
            } else {
                uint64_t actual_balance = token_bucket_available(
                    &actual, supplied_time);
                reference_refill(&expected, supplied_time);
                assert(actual_balance == expected.tokens);
            }

            assert(actual.tokens == expected.tokens);
            assert(actual.last_refill_ns == expected.anchor);
            assert(actual.tokens <= actual.capacity);
            assert(actual.last_refill_ns >= old_anchor);
            assert(actual.capacity == capacity);
            assert(actual.refill_interval_ns == interval);
        }
    }
}

int main(void)
{
    test_differential_against_reference_model();
    puts("token bucket property tests passed: 48000 deterministic operations");
    return 0;
}
