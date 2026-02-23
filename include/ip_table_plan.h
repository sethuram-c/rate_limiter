#ifndef IP_TABLE_PLAN_H
#define IP_TABLE_PLAN_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/*
 * Phase 9: pure, SEQUENTIAL reference model for fixed-capacity IPv4 keys.
 * No concurrent reads/writes of slots are permitted. These definitions
 * specify probe order and expected semantics for the future atomic table.
 *
 * Addresses are canonical 32-bit IPv4 values, not strings or pointers.
 * Every uint32_t is a valid key, including 0 and UINT32_MAX. Occupancy
 * must therefore have its own representation; no IP value is a sentinel.
 */
typedef struct {
    uint32_t ip;
    bool occupied;
} ip_model_slot_t;

typedef enum {
    IP_PROBE_FOUND,
    IP_PROBE_EMPTY,
    IP_PROBE_FULL
} ip_probe_kind_t;

typedef struct {
    ip_probe_kind_t kind;
    size_t index;   /* capacity if the table is full */
    size_t probes;  /* number of slots inspected: 1 through capacity */
} ip_probe_result_t;

typedef enum {
    IP_INSERT_NEW,
    IP_INSERT_EXISTING,
    IP_INSERT_FULL
} ip_insert_kind_t;

typedef struct {
    ip_insert_kind_t kind;
    size_t index;   /* capacity if the table is full */
    size_t probes;
} ip_insert_result_t;

/* Requires a nonzero power-of-two slot count. On failure, out unchanged. */
bool ip_table_plan_valid_capacity(size_t capacity);
bool ip_table_plan_start(uint32_t ip, size_t capacity, size_t *out_index);

/* Deterministic multiplicative/avalanche hash; no random seed in Phase 9. */
uint32_t ip_table_plan_hash(uint32_t ip);

/* Pure sequential probe: stop on matching key, first empty, or full scan. */
bool ip_table_plan_probe(const ip_model_slot_t *slots, size_t capacity,
                         uint32_t ip, ip_probe_result_t *out);

/* Mutates the model ONLY; output remains unchanged on invalid arguments. */
bool ip_table_model_insert(ip_model_slot_t *slots, size_t capacity,
                           uint32_t ip, ip_insert_result_t *out);

#endif
