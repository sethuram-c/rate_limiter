#include "ip_table_plan.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define CAP 128U
#define ROUNDS 200U
#define STEPS 300U

static uint32_t rng_state = UINT32_C(0x2d873bf1);

static uint32_t next_random(void)
{
    rng_state ^= rng_state << 13U;
    rng_state ^= rng_state >> 17U;
    rng_state ^= rng_state << 5U;
    return rng_state;
}

static bool reference_contains(const uint32_t *keys, size_t count, uint32_t ip)
{
    size_t i;
    for (i = 0U; i < count; ++i) {
        if (keys[i] == ip) {
            return true;
        }
    }
    return false;
}

static void test_input_and_encoding_boundaries(void)
{
    ip_model_slot_t slots[4] = {{0}};
    ip_probe_result_t probe = {IP_PROBE_FULL, 777U, 888U};
    ip_insert_result_t insertion = {IP_INSERT_FULL, 333U, 444U};
    size_t index = 99U;
    size_t i;

    assert(!ip_table_plan_valid_capacity(0U));
    assert(ip_table_plan_valid_capacity(1U));
    assert(ip_table_plan_valid_capacity(4U));
    assert(!ip_table_plan_valid_capacity(3U));
    assert(!ip_table_plan_valid_capacity(6U));
    assert(!ip_table_plan_start(1U, 3U, &index) && index == 99U);
    assert(!ip_table_plan_start(1U, 4U, NULL));
    assert(!ip_table_plan_probe(NULL, 4U, 1U, &probe));
    assert(!ip_table_plan_probe(slots, 3U, 1U, &probe));
    assert(!ip_table_plan_probe(slots, 4U, 1U, NULL));
    assert(probe.kind == IP_PROBE_FULL && probe.index == 777U &&
           probe.probes == 888U);
    assert(!ip_table_model_insert(NULL, 4U, 1U, &insertion));
    assert(!ip_table_model_insert(slots, 4U, 1U, NULL));
    assert(insertion.kind == IP_INSERT_FULL && insertion.index == 333U &&
           insertion.probes == 444U);

    assert(ip_table_model_insert(slots, 4U, 0U, &insertion));
    assert(insertion.kind == IP_INSERT_NEW);
    assert(ip_table_model_insert(slots, 4U, UINT32_MAX, &insertion));
    assert(insertion.kind == IP_INSERT_NEW);
    assert(ip_table_plan_probe(slots, 4U, 0U, &probe));
    assert(probe.kind == IP_PROBE_FOUND);
    assert(ip_table_plan_probe(slots, 4U, UINT32_MAX, &probe));
    assert(probe.kind == IP_PROBE_FOUND);
    for (i = 0U; i < 4U; ++i) {
        if (slots[i].occupied) {
            assert(slots[i].ip == 0U || slots[i].ip == UINT32_MAX);
        }
    }
}

static void test_duplicate_collision_and_wraparound(void)
{
    ip_model_slot_t slots[8] = {{0}};
    uint32_t keys[4];
    size_t found = 0U;
    size_t k;
    ip_insert_result_t insertion;
    ip_probe_result_t probe;

    /* Find 4 distinct IPs that all hash to the final slot, forcing wrap. */
    for (k = 0U; found < 4U && k < 100000U; ++k) {
        if ((ip_table_plan_hash((uint32_t)k) & 7U) == 7U) {
            keys[found++] = (uint32_t)k;
        }
    }
    assert(found == 4U);
    for (k = 0U; k < found; ++k) {
        assert(ip_table_model_insert(slots, 8U, keys[k], &insertion));
        assert(insertion.kind == IP_INSERT_NEW);
        assert(insertion.index == ((7U + k) & 7U));
        assert(insertion.probes == k + 1U);
    }
    for (k = 0U; k < found; ++k) {
        assert(ip_table_model_insert(slots, 8U, keys[k], &insertion));
        assert(insertion.kind == IP_INSERT_EXISTING);
        assert(insertion.index == ((7U + k) & 7U));
    }
    assert(ip_table_plan_probe(slots, 8U, UINT32_MAX, &probe));
    assert(probe.probes >= 1U && probe.probes <= 8U);
}

static void test_full_table_and_capacity_one(void)
{
    ip_model_slot_t slots[4] = {{0}};
    ip_model_slot_t single[1] = {{0}};
    ip_insert_result_t result;
    uint32_t keys[4] = {0U, 1U, 2U, 3U};
    size_t k;

    for (k = 0U; k < 4U; ++k) {
        assert(ip_table_model_insert(slots, 4U, keys[k], &result));
        assert(result.kind == IP_INSERT_NEW);
    }
    assert(ip_table_model_insert(slots, 4U, 4U, &result));
    assert(result.kind == IP_INSERT_FULL && result.index == 4U &&
           result.probes == 4U);
    assert(ip_table_model_insert(slots, 4U, 0U, &result));
    assert(result.kind == IP_INSERT_EXISTING);
    assert(ip_table_model_insert(single, 1U, UINT32_MAX, &result));
    assert(result.kind == IP_INSERT_NEW);
    assert(ip_table_model_insert(single, 1U, UINT32_MAX, &result));
    assert(result.kind == IP_INSERT_EXISTING);
    assert(ip_table_model_insert(single, 1U, 0U, &result));
    assert(result.kind == IP_INSERT_FULL);
}

static void test_reference_model(void)
{
    ip_model_slot_t slots[CAP];
    uint32_t reference[CAP];
    size_t round;
    size_t checked = 0U;

    for (round = 0U; round < ROUNDS; ++round) {
        size_t count = 0U;
        size_t iteration;
        memset(slots, 0, sizeof slots);
        for (iteration = 0U; iteration < STEPS; ++iteration) {
            uint32_t ip = next_random();
            bool exists;
            ip_insert_result_t ins;
            ip_probe_result_t probe;
            size_t i;
            if (iteration % 17U == 0U) {
                ip = 0U;
            } else if (iteration % 31U == 0U) {
                ip = UINT32_MAX;
            } else if (iteration % 5U == 0U && count > 0U) {
                ip = reference[next_random() % count];
            }
            exists = reference_contains(reference, count, ip);
            assert(ip_table_model_insert(slots, CAP, ip, &ins));
            if (exists) {
                assert(ins.kind == IP_INSERT_EXISTING);
            } else if (count == CAP) {
                assert(ins.kind == IP_INSERT_FULL);
            } else {
                assert(ins.kind == IP_INSERT_NEW);
                reference[count++] = ip;
            }
            assert(ip_table_plan_probe(slots, CAP, ip, &probe));
            if (ins.kind == IP_INSERT_FULL) {
                assert(probe.kind == IP_PROBE_FULL);
            } else {
                assert(probe.kind == IP_PROBE_FOUND);
                assert(slots[probe.index].ip == ip);
            }
            for (i = 0U; i < CAP; ++i) {
                if (slots[i].occupied) {
                    assert(reference_contains(reference, count, slots[i].ip));
                }
            }
            ++checked;
        }
    }
    printf("IP table plan tests passed: %zu reference-model operations\n", checked);
}

int main(void)
{
    test_input_and_encoding_boundaries();
    test_duplicate_collision_and_wraparound();
    test_full_table_and_capacity_one();
    test_reference_model();
    return 0;
}
