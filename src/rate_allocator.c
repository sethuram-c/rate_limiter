#include "rate_allocator.h"

#include <stdint.h>
#include <stdlib.h>

bool rl_allocator_valid(const rl_allocator_t *allocator)
{
    return allocator == NULL ||
           (allocator->allocate != NULL && allocator->deallocate != NULL);
}

void *rl_allocator_alloc(const rl_allocator_t *allocator,
                         size_t alignment, size_t bytes)
{
    if (!rl_allocator_valid(allocator) || bytes == 0U || alignment == 0U ||
        (alignment & (alignment - 1U)) != 0U) {
        return NULL;
    }
    if (allocator != NULL) {
        return allocator->allocate(allocator->context, alignment, bytes);
    }
    if (alignment <= _Alignof(max_align_t)) {
        return malloc(bytes);
    }
    /* aligned_alloc requires a multiple of alignment. Do not round up:
     * initialization validates each C11 array size ahead of time. */
    if (bytes % alignment != 0U) {
        return NULL;
    }
    return aligned_alloc(alignment, bytes);
}

void rl_allocator_free(const rl_allocator_t *allocator, void *memory)
{
    if (memory == NULL) {
        return;
    }
    if (allocator != NULL && allocator->deallocate != NULL) {
        allocator->deallocate(allocator->context, memory);
    } else if (allocator == NULL) {
        free(memory);
    }
}
