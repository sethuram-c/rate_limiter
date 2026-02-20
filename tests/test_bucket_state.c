#include "bucket_state.h"
#include "token_bucket.h"

#include <assert.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdint.h>

#define NS_PER_MS UINT64_C(1000000)

static uint64_t pack_ok(uint64_t time_ms, uint16_t tokens)
{
    uint64_t result = 0U;
    assert(bucket_state_pack(time_ms, tokens, &result));
    return result;
}

static void expect_state(uint64_t state, uint64_t time_ms, uint16_t tokens)
{
    uint64_t actual_ms = 0U;
    uint16_t actual_tokens = 0U;
    bucket_state_unpack(state, &actual_ms, &actual_tokens);
    assert(actual_ms == time_ms);
    assert(actual_tokens == tokens);
}

static void test_encoding(void)
{
    uint64_t output = UINT64_C(0x12345678);
    uint64_t anchor = 0U;
    uint16_t tokens = 0U;
    const uint64_t max_time = BUCKET_STATE_MAX_TIME_MS;

    assert(bucket_state_pack(0U, 0U, &output));
    assert(output == 0U);
    assert(bucket_state_pack(max_time, UINT16_MAX, &output));
    assert(output == UINT64_MAX);
    bucket_state_unpack(output, &anchor, &tokens);
    assert(anchor == max_time && tokens == UINT16_MAX);
    assert(!bucket_state_pack(max_time + 1U, 1U, &output));
    assert(output == UINT64_MAX);
    assert(!bucket_state_pack(0U, 1U, NULL));
    bucket_state_unpack(output, NULL, NULL);
}

static void test_validation(void)
{
    bucket_policy_t policy = {5U, 100U};
    bucket_policy_t invalid_capacity = {0U, 100U};
    bucket_policy_t invalid_interval = {5U, 0U};
    bucket_policy_t invalid_range = {5U, BUCKET_STATE_MAX_TIME_MS + 1U};
    bucket_transition_t step = {UINT64_C(0x123), true};
    uint64_t state = UINT64_C(0x4242);
    assert(!bucket_state_initial(NULL, 10U, &state));
    assert(!bucket_state_initial(&invalid_capacity, 10U, &state));
    assert(!bucket_state_initial(&invalid_interval, 10U, &state));
    assert(!bucket_state_initial(&invalid_range, 10U, &state));
    assert(!bucket_state_initial(&policy, BUCKET_STATE_MAX_TIME_MS + 1U, &state));
    assert(!bucket_state_initial(&policy, 10U, NULL));
    assert(state == UINT64_C(0x4242));
    assert(bucket_state_initial(&policy, 10U, &state));
    expect_state(state, 10U, 5U);
    assert(!bucket_state_plan(&policy, state, 0U, 10U, &step));
    assert(!bucket_state_plan(NULL, state, 1U, 10U, &step));
    assert(!bucket_state_plan(&invalid_capacity, state, 1U, 10U, &step));
    assert(!bucket_state_plan(&policy, state, 1U,
                              BUCKET_STATE_MAX_TIME_MS + 1U, &step));
    assert(!bucket_state_plan(&policy, state, 1U, 10U, NULL));
    assert(!bucket_state_plan(&policy, pack_ok(10U, 6U), 1U, 10U, &step));
    assert(step.next_state == UINT64_C(0x123) && step.allowed);
}

static void test_deterministic_transitions(void)
{
    bucket_policy_t policy = {3U, 100U};
    bucket_transition_t step;
    uint64_t state;

    assert(bucket_state_initial(&policy, 1000U, &state));
    assert(bucket_state_plan(&policy, state, 2U, 1000U, &step));
    assert(step.allowed);
    expect_state(step.next_state, 1000U, 1U);
    state = step.next_state;

    assert(bucket_state_plan(&policy, state, 2U, 1099U, &step));
    assert(!step.allowed);
    expect_state(step.next_state, 1000U, 1U);
    state = step.next_state;

    assert(bucket_state_plan(&policy, state, 2U, 1150U, &step));
    assert(step.allowed);
    expect_state(step.next_state, 1100U, 0U);
    state = step.next_state;

    assert(bucket_state_plan(&policy, state, 1U, 1199U, &step));
    assert(!step.allowed);
    expect_state(step.next_state, 1100U, 0U);
    state = step.next_state;

    assert(bucket_state_plan(&policy, state, 1U, 1200U, &step));
    assert(step.allowed);
    expect_state(step.next_state, 1200U, 0U);
    state = step.next_state;

    /* Stale timestamps do not rewind the anchor. */
    assert(bucket_state_plan(&policy, state, 1U, 1100U, &step));
    assert(!step.allowed);
    expect_state(step.next_state, 1200U, 0U);

    /* Rejected requests still accumulate refill and preserve partial credit. */
    assert(bucket_state_plan(&policy, state, 4U, 1451U, &step));
    assert(!step.allowed);
    expect_state(step.next_state, 1400U, 2U);
}

static void test_capacity_and_extreme_timestamps(void)
{
    const uint64_t max = BUCKET_STATE_MAX_TIME_MS;
    bucket_policy_t policy = {UINT16_MAX, 1U};
    bucket_transition_t step;
    uint64_t state = pack_ok(max - 10U, 0U);
    assert(bucket_state_plan(&policy, state, UINT32_MAX, max, &step));
    assert(!step.allowed);
    expect_state(step.next_state, max, 10U);
    assert(bucket_state_plan(&policy, state, 1U, max, &step));
    assert(step.allowed);
    expect_state(step.next_state, max, 9U);

    state = pack_ok(0U, UINT16_MAX);
    assert(bucket_state_plan(&policy, state, 1U, max, &step));
    assert(step.allowed);
    expect_state(step.next_state, max, UINT16_MAX - 1U);
    assert(bucket_state_plan(&policy, state, UINT32_MAX, max, &step));
    assert(!step.allowed);
    expect_state(step.next_state, max, UINT16_MAX);

    policy.refill_interval_ms = max;
    state = pack_ok(0U, 0U);
    assert(bucket_state_plan(&policy, state, 1U, max - 1U, &step));
    assert(!step.allowed);
    expect_state(step.next_state, 0U, 0U);
    assert(bucket_state_plan(&policy, state, 1U, max, &step));
    assert(step.allowed);
    expect_state(step.next_state, max, 0U);
}

/* Independent Phase 4 reference model, using identical millisecond semantics.
 * 24000 reproducible mixed operations compare every token, anchor and verdict.
 */
static uint64_t prng(uint64_t *seed)
{
    uint64_t x = *seed;
    x ^= x << 13;
    x ^= x >> 7;
    x ^= x << 17;
    *seed = x;
    return x;
}

static void test_reference_agreement(void)
{
    static const uint16_t capacities[] = {1U, 2U, 7U, 97U, 1024U, UINT16_MAX};
    static const uint64_t intervals[] = {1U, 2U, 7U, 101U, 1000U, 8191U};
    uint64_t seed = UINT64_C(0x9e3779b97f4a7c15);
    unsigned cases = 0U;
    unsigned idx;
    for (idx = 0U; idx < 6U; ++idx) {
        bucket_policy_t policy = {capacities[idx], intervals[idx]};
        token_bucket_t sequential;
        uint64_t state;
        uint64_t now_ms = 1000U;
        unsigned round;
        assert(token_bucket_init(&sequential, policy.capacity,
                                 policy.refill_interval_ms * NS_PER_MS,
                                 now_ms * NS_PER_MS) == 0);
        assert(bucket_state_initial(&policy, now_ms, &state));
        for (round = 0U; round < 4000U; ++round) {
            uint64_t sample;
            uint64_t anchor;
            uint16_t balance;
            uint32_t requested;
            bool reference_allowed;
            bucket_transition_t planned;
            sample = prng(&seed);
            if ((sample & UINT64_C(7)) != 0U) {
                now_ms += (sample >> 8) % 200U;
            }
            sample = now_ms;
            if ((prng(&seed) & UINT64_C(15)) == 0U && sample >= 50U) {
                sample -= 50U; /* stale caller time */
            }
            requested = (uint32_t)(prng(&seed) % ((uint64_t)policy.capacity + 2U)) + 1U;
            reference_allowed = token_bucket_try_consume_n(
                &sequential, requested, sample * NS_PER_MS);
            assert(bucket_state_plan(&policy, state, requested, sample, &planned));
            assert(planned.allowed == reference_allowed);
            state = planned.next_state;
            bucket_state_unpack(state, &anchor, &balance);
            assert(balance == sequential.tokens);
            assert(anchor * NS_PER_MS == sequential.last_refill_ns);
            assert(balance <= policy.capacity);
            ++cases;
        }
    }
    assert(cases == 24000U);
    printf("bucket state tests passed: %u reference transitions\n", cases);
}

int main(void)
{
    test_encoding();
    test_validation();
    test_deterministic_transitions();
    test_capacity_and_extreme_timestamps();
    test_reference_agreement();
    return 0;
}
