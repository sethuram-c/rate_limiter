#include "ip_atomic_table.h"
#include "ip_table_plan.h"

#include <stdlib.h>

#define IP_ATOMIC_OCCUPIED (UINT64_C(1) << 32)

static bool valid(const ip_atomic_table_t *table)
{
    return table != NULL && table->slots != NULL &&
           ip_table_plan_valid_capacity(table->capacity);
}

static uint64_t encode(uint32_t ip)
{
    return IP_ATOMIC_OCCUPIED | (uint64_t)ip;
}

bool ip_atomic_table_init(ip_atomic_table_t *table, size_t capacity)
{
    ip_atomic_slot_t *slots;
    size_t i;

    if (table == NULL || table->slots != NULL || table->capacity != 0U ||
        !ip_table_plan_valid_capacity(capacity) ||
        capacity > SIZE_MAX / sizeof(ip_atomic_slot_t)) {
        return false;
    }
    slots = malloc(capacity * sizeof(*slots));
    if (slots == NULL) {
        return false;
    }
    for (i = 0U; i < capacity; ++i) {
        atomic_init(&slots[i].key, UINT64_C(0));
    }
    if (!atomic_is_lock_free(&slots[0].key)) {
        free(slots);
        return false;
    }
    table->slots = slots;
    table->capacity = capacity;
    return true;
}

void ip_atomic_table_destroy(ip_atomic_table_t *table)
{
    if (table != NULL) {
        free(table->slots);
        table->slots = NULL;
        table->capacity = 0U;
    }
}

bool ip_atomic_table_get_or_insert(ip_atomic_table_t *table, uint32_t ip,
                                   ip_atomic_insert_result_t *out)
{
    size_t start;
    size_t step;
    uint64_t wanted = encode(ip);
    ip_atomic_insert_result_t result;

    if (out == NULL || !valid(table) ||
        !ip_table_plan_start(ip, table->capacity, &start)) {
        return false;
    }
    for (step = 0U; step < table->capacity; ++step) {
        size_t index = (start + step) & (table->capacity - 1U);
        _Atomic uint64_t *slot = &table->slots[index].key;
        uint64_t observed = atomic_load_explicit(slot, memory_order_seq_cst);

        if (observed == UINT64_C(0)) {
            uint64_t empty = UINT64_C(0);
            /* Strong CAS: no spurious failure; a lost race reveals winner. */
            if (atomic_compare_exchange_strong_explicit(
                    slot, &empty, wanted,
                    memory_order_seq_cst, memory_order_seq_cst)) {
                result.kind = IP_ATOMIC_INSERTED;
                result.index = index;
                result.probes = step + 1U;
                *out = result;
                return true;
            }
            observed = empty; /* CAS failure refreshes the observed value. */
        }
        if (observed == wanted) {
            result.kind = IP_ATOMIC_EXISTS;
            result.index = index;
            result.probes = step + 1U;
            *out = result;
            return true;
        }
    }
    result.kind = IP_ATOMIC_FULL;
    result.index = table->capacity;
    result.probes = table->capacity;
    *out = result;
    return true;
}

bool ip_atomic_table_find(const ip_atomic_table_t *table, uint32_t ip,
                          bool *found, size_t *index)
{
    size_t start;
    size_t step;
    uint64_t wanted = encode(ip);

    if (found == NULL || index == NULL || !valid(table) ||
        !ip_table_plan_start(ip, table->capacity, &start)) {
        return false;
    }
    for (step = 0U; step < table->capacity; ++step) {
        size_t current = (start + step) & (table->capacity - 1U);
        uint64_t observed = atomic_load_explicit(&table->slots[current].key,
                                                  memory_order_seq_cst);
        if (observed == wanted) {
            *found = true;
            *index = current;
            return true;
        }
        if (observed == UINT64_C(0)) {
            *found = false;
            *index = table->capacity;
            return true;
        }
    }
    *found = false;
    *index = table->capacity;
    return true;
}

size_t ip_atomic_table_count_quiescent(const ip_atomic_table_t *table)
{
    size_t i;
    size_t count = 0U;
    if (!valid(table)) {
        return 0U;
    }
    for (i = 0U; i < table->capacity; ++i) {
        if (atomic_load_explicit(&table->slots[i].key,
                                 memory_order_seq_cst) != UINT64_C(0)) {
            ++count;
        }
    }
    return count;
}
