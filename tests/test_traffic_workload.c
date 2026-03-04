#include "traffic_workload.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static void test_validation_and_no_mutation(void)
{
    traffic_workload_t config = { 42U, 16U, TRAFFIC_UNIFORM };
    uint32_t ip = UINT32_C(0xdeadbeef);
    assert(!traffic_workload_ip(NULL, 0U, 0U, &ip));
    assert(!traffic_workload_ip(&config, 0U, 0U, NULL));
    assert(ip == UINT32_C(0xdeadbeef));
    config.ip_count = 0U;
    assert(!traffic_workload_ip(&config, 0U, 0U, &ip));
    assert(ip == UINT32_C(0xdeadbeef));
    config.ip_count = 65537U;
    assert(!traffic_workload_valid(&config));
    config.ip_count = 16U;
    config.pattern = (traffic_pattern_t)99;
    assert(!traffic_workload_valid(&config));
    assert(strcmp(traffic_pattern_name(config.pattern), "invalid") == 0);
}

static void test_determinism_and_ranges(void)
{
    const traffic_pattern_t patterns[] = {
        TRAFFIC_HOT, TRAFFIC_UNIFORM, TRAFFIC_SKEWED
    };
    size_t p;
    for (p = 0U; p < sizeof(patterns) / sizeof(patterns[0]); ++p) {
        traffic_workload_t config = { UINT64_C(20270101), 64U, patterns[p] };
        uint32_t worker;
        for (worker = 0U; worker < 8U; ++worker) {
            uint64_t i;
            for (i = 0U; i < 5000U; ++i) {
                uint32_t first, second;
                assert(traffic_workload_ip(&config, worker, i, &first));
                assert(traffic_workload_ip(&config, worker, i, &second));
                assert(first == second);
                assert(first >= UINT32_C(0xc6120000));
                assert(first < UINT32_C(0xc6120000) + config.ip_count);
                if (config.pattern == TRAFFIC_HOT) {
                    assert(first == UINT32_C(0xc6120000));
                }
            }
        }
    }
}

static void test_distribution(void)
{
    traffic_workload_t config = { 7U, 32U, TRAFFIC_SKEWED };
    uint64_t counts[32] = {0U};
    uint64_t i;
    uint64_t hot = 0U;
    for (i = 0U; i < 20000U; ++i) {
        uint32_t ip;
        assert(traffic_workload_ip(&config, 2U, i, &ip));
        ++counts[ip - UINT32_C(0xc6120000)];
    }
    for (i = 0U; i < 4U; ++i) {
        hot += counts[i];
    }
    assert(hot > 15000U && hot < 19000U);
    config.pattern = TRAFFIC_UNIFORM;
    memset(counts, 0, sizeof(counts));
    for (i = 0U; i < 20000U; ++i) {
        uint32_t ip;
        assert(traffic_workload_ip(&config, 2U, i, &ip));
        ++counts[ip - UINT32_C(0xc6120000)];
    }
    for (i = 0U; i < 32U; ++i) {
        assert(counts[i] > 300U && counts[i] < 1000U);
    }
}

static void test_mixed_costs(void)
{
    traffic_workload_t config = { 20270104U, 32U, TRAFFIC_SKEWED };
    uint64_t triples = 0U;
    uint64_t i;
    uint32_t cost = 99U;
    assert(!traffic_workload_cost(NULL, 0U, 0U, true, &cost));
    assert(cost == 99U);
    assert(!traffic_workload_cost(&config, 0U, 0U, true, NULL));
    for (i = 0U; i < 20000U; ++i) {
        uint32_t first, second, unit;
        assert(traffic_workload_cost(&config, 2U, i, true, &first));
        assert(traffic_workload_cost(&config, 2U, i, true, &second));
        assert(traffic_workload_cost(&config, 2U, i, false, &unit));
        assert(first == second && (first == 1U || first == 3U));
        assert(unit == 1U);
        triples += first == 3U ? 1U : 0U;
    }
    assert(triples > 3500U && triples < 4500U);
}

int main(void)
{
    test_validation_and_no_mutation();
    test_determinism_and_ranges();
    test_distribution();
    test_mixed_costs();
    puts("traffic workload tests passed: 120000 deterministic IP samples + 20000 weighted samples");
    return 0;
}
