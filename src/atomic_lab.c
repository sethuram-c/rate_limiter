#include "atomic_lab.h"

#include <stddef.h>

bool atomic_counter_init(atomic_counter_t *counter, uint_fast64_t initial)
{
    if (counter == NULL) {
        return false;
    }
    atomic_init(&counter->value, initial);
    return true;
}

bool atomic_counter_increment_bounded(atomic_counter_t *counter,
                                       uint_fast64_t limit)
{
    uint_fast64_t observed;

    if (counter == NULL) {
        return false;
    }

    observed = atomic_load_explicit(&counter->value, memory_order_relaxed);
    while (observed < limit) {
        /* A weak CAS may fail spuriously; always retry with its new observed
         * value. Relaxed is valid for this standalone numerical invariant.
         */
        if (atomic_compare_exchange_weak_explicit(
                &counter->value, &observed, observed + 1U,
                memory_order_relaxed, memory_order_relaxed)) {
            return true;
        }
    }
    return false;
}

uint_fast64_t atomic_counter_get(const atomic_counter_t *counter)
{
    return atomic_load_explicit(&counter->value, memory_order_relaxed);
}

bool atomic_counter_is_lock_free(atomic_counter_t *counter)
{
    return atomic_is_lock_free(&counter->value);
}

bool atomic_mailbox_init(atomic_mailbox_t *mailbox)
{
    if (mailbox == NULL) {
        return false;
    }
    mailbox->payload = 0U;
    atomic_init(&mailbox->published, false);
    return true;
}

void atomic_mailbox_publish_once(atomic_mailbox_t *mailbox,
                                 uint64_t payload)
{
    /* Contract: exactly one call by one writer after init, no reuse. */
    mailbox->payload = payload;
    atomic_store_explicit(&mailbox->published, true, memory_order_release);
}

bool atomic_mailbox_try_read(const atomic_mailbox_t *mailbox,
                             uint64_t *out)
{
    if (mailbox == NULL || out == NULL) {
        return false;
    }
    if (!atomic_load_explicit(&mailbox->published, memory_order_acquire)) {
        return false;
    }
    *out = mailbox->payload;
    return true;
}
