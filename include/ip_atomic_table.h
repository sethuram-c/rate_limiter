#ifndef IP_ATOMIC_TABLE_H
#define IP_ATOMIC_TABLE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdatomic.h>

/*
 * Phase 10: fixed-capacity, insert-only, concurrent IPv4 key index.
 *
 * All 2^32 IPv4 values are legal. Empty slots contain 0; an occupied slot
 * contains UINT64_C(1)<<32 | ip. The high bit of the low 33 bits is the
 * occupancy tag, NOT a reserved IP address.
 *
 * Lifecycle: initialize on one thread before sharing; no resize, deletion,
 * move, destroy, or reinitialization while any thread is accessing the table.
 * The caller must ensure no concurrent operation outlives destroy().
 *
 * No bucket payload is attached in Phase 10. The atomic key is safely
 * published; per-slot bucket initialization is tackled in Phase 11.
 */
typedef struct {
    _Atomic uint64_t key;
} ip_atomic_slot_t;

typedef struct {
    size_t capacity;
    ip_atomic_slot_t *slots;
} ip_atomic_table_t;

typedef enum {
    IP_ATOMIC_INSERTED,
    IP_ATOMIC_EXISTS,
    IP_ATOMIC_FULL
} ip_atomic_insert_kind_t;

typedef struct {
    ip_atomic_insert_kind_t kind;
    size_t index;  /* capacity on FULL */
    size_t probes; /* 1..capacity */
} ip_atomic_insert_result_t;

/* table must initially be {0}; capacity is a nonzero power of two.
 * Allocation happens HERE, not on the concurrent insertion path.
 * Fails if the platform lacks lock-free 64-bit atomic slots. */
bool ip_atomic_table_init(ip_atomic_table_t *table, size_t capacity);

/* Caller must first stop/join all table users. */
void ip_atomic_table_destroy(ip_atomic_table_t *table);

/* Insert an IP once, or find its existing slot. Other keys cannot evict it.
 * Returns false on bad arguments. On false, *out stays unchanged. */
bool ip_atomic_table_get_or_insert(ip_atomic_table_t *table, uint32_t ip,
                                   ip_atomic_insert_result_t *out);

/* On valid calls, reports whether the IP was visible during this lookup.
 * Concurrent insertion may happen after a NOT FOUND result. Outputs remain
 * untouched on invalid arguments. */
bool ip_atomic_table_find(const ip_atomic_table_t *table, uint32_t ip,
                          bool *found, size_t *index);

/* Quiescent-only convenience; a concurrent count is not a consistent snapshot. */
size_t ip_atomic_table_count_quiescent(const ip_atomic_table_t *table);

#endif
