#ifndef TRAFFIC_WORKLOAD_H
#define TRAFFIC_WORKLOAD_H

#include <stdbool.h>
#include <stdint.h>

/* Phase 18: stateless, deterministic synthetic IPv4 traffic selection.
 * The same (seed, worker, iteration, pattern, ip_count) yields the same IP
 * regardless of thread interleaving, process clock or libc random state.
 * These are synthetic BENCHMARK keys, not real client identities. */
typedef enum {
    TRAFFIC_HOT,
    TRAFFIC_UNIFORM,
    TRAFFIC_SKEWED
} traffic_pattern_t;

typedef struct {
    uint64_t seed;
    uint32_t ip_count; /* 1..65536 distinct 198.18.0.0/15 keys */
    traffic_pattern_t pattern;
} traffic_workload_t;

bool traffic_workload_valid(const traffic_workload_t *config);
const char *traffic_pattern_name(traffic_pattern_t pattern);

/* Does not change *out_ip on invalid arguments. No mutable RNG state. */
bool traffic_workload_ip(const traffic_workload_t *config,
                         uint32_t worker_id, uint64_t iteration,
                         uint32_t *out_ip);

/* Cost profile: unit=always one token, mixed=80% one + 20% three.
 * Pure, deterministic and independent of the selected IP or thread schedule.
 * On failure does not modify *out_cost. */
bool traffic_workload_cost(const traffic_workload_t *config,
                           uint32_t worker_id, uint64_t iteration,
                           bool mixed, uint32_t *out_cost);

#endif
