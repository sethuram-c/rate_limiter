#ifndef RATE_ALLOCATOR_H
#define RATE_ALLOCATOR_H

#include <stdbool.h>
#include <stddef.h>

/* Phase 24: allocation is a QUIESCENT initialization-time concern.
 * NULL allocator means the C11 standard library default. Custom allocator
 * callback pairs must remain valid until the owning table/limiter is destroyed.
 * allocate() must return storage aligned to the requested power-of-two
 * alignment, or NULL. deallocate() must accept the returned storage.
 * No allocator callback is invoked by a request/lookup/consume operation.
 */
typedef void *(*rl_allocate_fn)(void *context, size_t alignment, size_t bytes);
typedef void (*rl_deallocate_fn)(void *context, void *memory);

typedef struct {
    rl_allocate_fn allocate;
    rl_deallocate_fn deallocate;
    void *context;
} rl_allocator_t;

bool rl_allocator_valid(const rl_allocator_t *allocator);
void *rl_allocator_alloc(const rl_allocator_t *allocator,
                         size_t alignment, size_t bytes);
void rl_allocator_free(const rl_allocator_t *allocator, void *memory);

#endif
