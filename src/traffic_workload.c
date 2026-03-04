#include "traffic_workload.h"
#include <stddef.h>

#define TRAFFIC_MAX_IPS UINT32_C(65536)
#define TRAFFIC_IP_BASE UINT32_C(0xc6120000) /* 198.18.0.0 */

/* A deterministic 64-bit avalanche mixer; NOT cryptographic. */
static uint64_t mix64(uint64_t x)
{
    x ^= x >> 30;
    x *= UINT64_C(0xbf58476d1ce4e5b9);
    x ^= x >> 27;
    x *= UINT64_C(0x94d049bb133111eb);
    return x ^ (x >> 31);
}

bool traffic_workload_valid(const traffic_workload_t *config)
{
    return config != NULL && config->ip_count >= 1U &&
           config->ip_count <= TRAFFIC_MAX_IPS &&
           (config->pattern == TRAFFIC_HOT ||
            config->pattern == TRAFFIC_UNIFORM ||
            config->pattern == TRAFFIC_SKEWED);
}

const char *traffic_pattern_name(traffic_pattern_t pattern)
{
    switch (pattern) {
    case TRAFFIC_HOT: return "hot";
    case TRAFFIC_UNIFORM: return "uniform";
    case TRAFFIC_SKEWED: return "skewed";
    }
    return "invalid";
}

bool traffic_workload_ip(const traffic_workload_t *config,
                         uint32_t worker_id, uint64_t iteration,
                         uint32_t *out_ip)
{
    uint64_t hash;
    uint32_t index;
    uint32_t hot_count;

    if (out_ip == NULL || !traffic_workload_valid(config)) {
        return false;
    }
    hash = mix64(config->seed ^
                 mix64((uint64_t)worker_id + UINT64_C(0x9e3779b97f4a7c15)) ^
                 mix64(iteration + UINT64_C(0xd1b54a32d192ed03)));
    if (config->pattern == TRAFFIC_HOT) {
        index = 0U;
    } else if (config->pattern == TRAFFIC_SKEWED && hash % 10U < 8U) {
        hot_count = config->ip_count < 4U ? config->ip_count : 4U;
        index = (uint32_t)(mix64(hash) % hot_count);
    } else {
        index = (uint32_t)(mix64(hash) % config->ip_count);
    }
    *out_ip = TRAFFIC_IP_BASE + index;
    return true;
}

bool traffic_workload_cost(const traffic_workload_t *config,
                           uint32_t worker_id, uint64_t iteration,
                           bool mixed, uint32_t *out_cost)
{
    uint64_t hash;
    if (!traffic_workload_valid(config) || out_cost == NULL) return false;
    hash = mix64(config->seed ^ UINT64_C(0x4f1bbcdc12345678) ^
                 mix64((uint64_t)worker_id) ^
                 mix64(iteration + UINT64_C(0xaf21de310f987655)));
    *out_cost = (mixed && hash % 5U == 0U) ? 3U : 1U;
    return true;
}
