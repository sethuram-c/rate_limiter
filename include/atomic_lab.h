#ifndef ATOMIC_LAB_H
#define ATOMIC_LAB_H

#include <stdbool.h>
#include <stdint.h>
#include <stdatomic.h>

/*
 * Phase 5 teaching module, separate from the sequential token bucket.
 * All participating threads must start AFTER initialization and must finish
 * BEFORE an object's storage is destroyed or reused.
 *
 * Bounded counter: relaxed ordering suffices because the counter does not
 * publish any other data. A successful CAS is the linearization point.
 */
typedef struct {
    atomic_uint_fast64_t value;
} atomic_counter_t;

/* Must be called before any concurrent access. NULL returns false. */
bool atomic_counter_init(atomic_counter_t *counter, uint_fast64_t initial);

/* One atomic increment if the current value is less than limit.
 * Returns false without modification when limit is reached or counter=NULL.
 * Cannot wrap even when limit=UINT_FAST64_MAX.
 */
bool atomic_counter_increment_bounded(atomic_counter_t *counter,
                                       uint_fast64_t limit);

/* Valid pointer and completed initialization required. */
uint_fast64_t atomic_counter_get(const atomic_counter_t *counter);

/* A true result indicates lock-free operations on this particular object. */
bool atomic_counter_is_lock_free(atomic_counter_t *counter);

/*
 * One-shot release/acquire publication: EXACTLY ONE writer may publish,
 * and it may do so EXACTLY ONCE. Many concurrent readers are allowed.
 * The payload is intentionally non-atomic: its writes happen-before reads
 * that observe published=true via acquire. Never reset or republish during
 * a live reader's access. Initialization happens before thread creation.
 */
typedef struct {
    uint64_t payload;
    atomic_bool published;
} atomic_mailbox_t;

bool atomic_mailbox_init(atomic_mailbox_t *mailbox);

/* One writer, one call, and non-NULL mailbox after init are required. */
void atomic_mailbox_publish_once(atomic_mailbox_t *mailbox,
                                 uint64_t payload);

/* Returns false if not ready (or invalid pointers), leaving *out unchanged. */
bool atomic_mailbox_try_read(const atomic_mailbox_t *mailbox,
                             uint64_t *out);

#endif
