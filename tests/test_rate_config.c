#include "rate_config.h"
#include "rate_service.h"

#include "ip_atomic_table.h"
#include "lockfree_bucket.h"

#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

static unsigned clock_calls;

static bool fixed_clock(void *context, uint64_t *now_ms)
{
    (void)context;
    ++clock_calls;
    if (now_ms == NULL) {
        return false;
    }
    *now_ms = 700U;
    return true;
}

static void test_planning_boundary_and_exact_budget(void)
{
    rate_config_t cfg = {8U, 3U, 100U, SIZE_MAX};
    rate_config_plan_t plan = {0};
    size_t expected = cfg.slots *
                      (sizeof(ip_atomic_slot_t) + sizeof(lf_bucket_t));

    assert(rate_config_plan(&cfg, &plan) == RATE_CONFIG_OK);
    assert(plan.slots == 8U);
    assert(plan.key_array_bytes == 8U * sizeof(ip_atomic_slot_t));
    assert(plan.bucket_array_bytes == 8U * sizeof(lf_bucket_t));
    assert(plan.total_array_bytes == expected);
    assert(plan.policy.capacity == 3U);
    assert(plan.policy.refill_interval_ms == 100U);

    cfg.memory_budget_bytes = expected;
    assert(rate_config_plan(&cfg, &plan) == RATE_CONFIG_OK);
    cfg.memory_budget_bytes = expected - 1U;
    assert(rate_config_plan(&cfg, &plan) == RATE_CONFIG_OVER_BUDGET);
    assert(plan.total_array_bytes == expected); /* unchanged on rejection */
    cfg.memory_budget_bytes = 0U;
    assert(rate_config_plan(&cfg, &plan) == RATE_CONFIG_OVER_BUDGET);
}

static void test_invalid_parameters_and_overflow(void)
{
    rate_config_t cfg = {8U, 2U, 10U, SIZE_MAX};
    rate_config_plan_t marker = {13U, 17U, 19U, 23U, {29U, 31U}};
    rate_config_plan_t plan = marker;
    size_t top_power_of_two = (SIZE_MAX / 2U) + 1U;

    assert(rate_config_plan(NULL, &plan) == RATE_CONFIG_NULL_ARGUMENT);
    assert(rate_config_plan(&cfg, NULL) == RATE_CONFIG_NULL_ARGUMENT);
    cfg.slots = 0U;
    assert(rate_config_plan(&cfg, &plan) == RATE_CONFIG_INVALID_SLOTS);
    cfg.slots = 3U;
    assert(rate_config_plan(&cfg, &plan) == RATE_CONFIG_INVALID_SLOTS);
    cfg.slots = 8U;
    cfg.capacity_tokens = 0U;
    assert(rate_config_plan(&cfg, &plan) == RATE_CONFIG_INVALID_CAPACITY);
    cfg.capacity_tokens = UINT16_MAX + 1U;
    assert(rate_config_plan(&cfg, &plan) == RATE_CONFIG_INVALID_CAPACITY);
    cfg.capacity_tokens = UINT16_MAX;
    cfg.refill_interval_ms = 0U;
    assert(rate_config_plan(&cfg, &plan) == RATE_CONFIG_INVALID_REFILL);
    cfg.refill_interval_ms = BUCKET_STATE_MAX_TIME_MS + 1U;
    assert(rate_config_plan(&cfg, &plan) == RATE_CONFIG_INVALID_REFILL);
    cfg.refill_interval_ms = BUCKET_STATE_MAX_TIME_MS;
    cfg.slots = top_power_of_two;
    assert(rate_config_plan(&cfg, &plan) == RATE_CONFIG_SIZE_OVERFLOW);
    assert(plan.slots == marker.slots && plan.total_array_bytes == 23U);
    assert(plan.policy.capacity == marker.policy.capacity);
    assert(plan.policy.refill_interval_ms == marker.policy.refill_interval_ms);
}

static void test_preflight_has_no_clock_or_allocation_side_effects(void)
{
    rate_service_t service = {0};
    rate_config_t cfg = {4U, 2U, 100U, SIZE_MAX};
    rate_config_plan_t plan;
    unsigned before;

    clock_calls = 0U;
    cfg.slots = 3U;
    assert(!rate_service_init_with_config(&service, &cfg, fixed_clock, NULL));
    assert(clock_calls == 0U);
    cfg.slots = 4U;
    cfg.capacity_tokens = 0U;
    assert(!rate_service_init_with_config(&service, &cfg, fixed_clock, NULL));
    assert(clock_calls == 0U);
    cfg.capacity_tokens = 2U;
    assert(rate_config_plan(&cfg, &plan) == RATE_CONFIG_OK);
    cfg.memory_budget_bytes = plan.total_array_bytes - 1U;
    assert(!rate_service_init_with_config(&service, &cfg, fixed_clock, NULL));
    assert(clock_calls == 0U);
    assert(service.clock == NULL && service.limiter.buckets == NULL &&
           service.limiter.index.slots == NULL);

    cfg.memory_budget_bytes = plan.total_array_bytes;
    assert(rate_service_init_with_config(&service, &cfg, fixed_clock, NULL));
    assert(clock_calls == 1U);
    assert(!rate_service_init_with_config(&service, &cfg, fixed_clock, NULL));
    assert(clock_calls == 1U);
    assert(rate_service_check(&service, 0U, 2U) == RATE_SERVICE_ALLOWED);
    assert(rate_service_check(&service, 0U, 1U) == RATE_SERVICE_DENIED);
    rate_service_destroy(&service);
    assert(service.clock == NULL && service.limiter.buckets == NULL);

    before = clock_calls;
    {
        bucket_policy_t bad_policy = {0U, 20U};
        /* Legacy path now validates bad bucket policies BEFORE clock reads. */
        assert(!rate_service_init(&service, 4U, &bad_policy,
                                  fixed_clock, NULL));
        assert(clock_calls == before);
    }
    rate_service_destroy(&service);
}

static void test_property_sweep(void)
{
    size_t slots;
    unsigned count = 0U;

    for (slots = 1U; slots <= 1024U; slots *= 2U) {
        unsigned cap;
        for (cap = 1U; cap <= 200U; ++cap) {
            rate_config_t cfg = {slots, cap, 1U + cap, SIZE_MAX};
            rate_config_plan_t plan;
            assert(rate_config_plan(&cfg, &plan) == RATE_CONFIG_OK);
            assert(plan.total_array_bytes == slots *
                   (sizeof(ip_atomic_slot_t) + sizeof(lf_bucket_t)));
            assert(plan.policy.capacity == cap);
            ++count;
        }
    }
    assert(count == 2200U);
    printf("configuration tests passed: %u valid planning cases\n", count);
}

int main(void)
{
    test_planning_boundary_and_exact_budget();
    test_invalid_parameters_and_overflow();
    test_preflight_has_no_clock_or_allocation_side_effects();
    test_property_sweep();
    assert(rate_config_status_string(RATE_CONFIG_OVER_BUDGET) != NULL);
    assert(rate_config_status_string((rate_config_status_t)999) != NULL);
    return 0;
}
