#include "ip_table_plan.h"

#include <stddef.h>

bool ip_table_plan_valid_capacity(size_t capacity)
{
    return capacity != 0U && (capacity & (capacity - 1U)) == 0U;
}

uint32_t ip_table_plan_hash(uint32_t ip)
{
    /* An integer avalanche finalizer. All uint32_t values remain valid keys. */
    ip ^= ip >> 16U;
    ip *= UINT32_C(0x7feb352d);
    ip ^= ip >> 15U;
    ip *= UINT32_C(0x846ca68b);
    ip ^= ip >> 16U;
    return ip;
}

bool ip_table_plan_start(uint32_t ip, size_t capacity, size_t *out_index)
{
    if (!ip_table_plan_valid_capacity(capacity) || out_index == NULL) {
        return false;
    }
    *out_index = (size_t)ip_table_plan_hash(ip) & (capacity - 1U);
    return true;
}

bool ip_table_plan_probe(const ip_model_slot_t *slots, size_t capacity,
                         uint32_t ip, ip_probe_result_t *out)
{
    size_t start;
    size_t step;
    ip_probe_result_t result;

    if (slots == NULL || out == NULL ||
        !ip_table_plan_start(ip, capacity, &start)) {
        return false;
    }

    for (step = 0U; step < capacity; ++step) {
        /* Linear probing with mask wraparound; no size_t addition overflow. */
        size_t index = (start + step) & (capacity - 1U);
        if (!slots[index].occupied) {
            result.kind = IP_PROBE_EMPTY;
            result.index = index;
            result.probes = step + 1U;
            *out = result;
            return true;
        }
        if (slots[index].ip == ip) {
            result.kind = IP_PROBE_FOUND;
            result.index = index;
            result.probes = step + 1U;
            *out = result;
            return true;
        }
    }

    result.kind = IP_PROBE_FULL;
    result.index = capacity;
    result.probes = capacity;
    *out = result;
    return true;
}

bool ip_table_model_insert(ip_model_slot_t *slots, size_t capacity,
                           uint32_t ip, ip_insert_result_t *out)
{
    ip_probe_result_t probe;
    ip_insert_result_t result;

    if (out == NULL || !ip_table_plan_probe(slots, capacity, ip, &probe)) {
        return false;
    }

    result.index = probe.index;
    result.probes = probe.probes;
    switch (probe.kind) {
    case IP_PROBE_FOUND:
        result.kind = IP_INSERT_EXISTING;
        break;
    case IP_PROBE_EMPTY:
        slots[probe.index].ip = ip;
        slots[probe.index].occupied = true;
        result.kind = IP_INSERT_NEW;
        break;
    case IP_PROBE_FULL:
        result.kind = IP_INSERT_FULL;
        break;
    default:
        return false;
    }
    *out = result;
    return true;
}
