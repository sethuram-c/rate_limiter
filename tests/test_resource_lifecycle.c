#include "ip_rate_limiter.h"
#include "rate_allocator.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

/* All counters describe allocation-time activities on one thread. */
typedef struct {
    size_t attempts;
    size_t successes;
    size_t releases;
    size_t fail_at;         /* 0: never fail, 1: first, 2: second */
    size_t peak_bytes;      /* requested array payload, not RSS */
    size_t total_bytes;
    size_t maximum_alignment;
} fault_allocator_state_t;

static void *fault_allocate(void *context, size_t alignment, size_t bytes)
{
    fault_allocator_state_t *state = context;
    void *ptr = NULL;
    ++state->attempts;
    if (state->attempts == state->fail_at) {
        return NULL;
    }
    assert(alignment != 0U && (alignment & (alignment - 1U)) == 0U);
    assert(bytes != 0U);
    if (alignment <= _Alignof(max_align_t)) {
        ptr = malloc(bytes);
    } else {
        /* posix_memalign accepts any size, unlike C11 aligned_alloc. */
        if (posix_memalign(&ptr, alignment, bytes) != 0) {
            ptr = NULL;
        }
    }
    if (ptr != NULL) {
        ++state->successes;
        state->total_bytes += bytes;
        state->peak_bytes = state->total_bytes;
        if (alignment > state->maximum_alignment) {
            state->maximum_alignment = alignment;
        }
    }
    return ptr;
}

static void fault_deallocate(void *context, void *memory)
{
    fault_allocator_state_t *state = context;
    assert(memory != NULL);
    ++state->releases;
    free(memory);
}

static void assert_empty(const ip_rate_limiter_t *limiter)
{
    assert(limiter->buckets == NULL);
    assert(limiter->index.slots == NULL);
    assert(limiter->index.capacity == 0U);
    assert(limiter->index.allocator.allocate == NULL);
    assert(limiter->allocator.allocate == NULL);
}

static void test_allocator_contract(void)
{
    const bucket_policy_t policy = {3U, 100U};
    rl_allocator_t broken = {fault_allocate, NULL, NULL};
    ip_rate_limiter_t limiter = {0};
    ip_atomic_table_t table = {0};
    assert(rl_allocator_valid(NULL));
    assert(!rl_allocator_valid(&broken));
    assert(!ip_rate_limiter_init_with_allocator(&limiter, 8U, &policy, 1000U,
                                                 &broken));
    assert(!ip_atomic_table_init_with_allocator(&table, 8U, &broken));
    assert_empty(&limiter);
    assert(table.slots == NULL && table.capacity == 0U);
    /* Bad configurations never call the allocator. */
    assert(!ip_rate_limiter_init_with_allocator(&limiter, 3U, &policy, 1000U,
                                                 NULL));
    assert_empty(&limiter);
    ip_atomic_table_destroy(&table);
}

static void test_each_failure_is_atomic(void)
{
    const bucket_policy_t policy = {3U, 100U};
    size_t fail_point;
    for (fail_point = 1U; fail_point <= 2U; ++fail_point) {
        ip_rate_limiter_t limiter = {0};
        fault_allocator_state_t state = {0};
        rl_allocator_t allocator = {fault_allocate, fault_deallocate, &state};
        state.fail_at = fail_point;
        assert(!ip_rate_limiter_init_with_allocator(&limiter, 8U, &policy,
                                                    1000U, &allocator));
        assert(state.attempts == fail_point);
        assert(state.successes == fail_point - 1U);
        assert(state.releases == state.successes);
        assert_empty(&limiter);
        /* idempotent destroy is allowed even after unsuccessful init. */
        ip_rate_limiter_destroy(&limiter);
        assert(state.releases == state.successes);
        /* The same zero-initialized instance can now be reused. */
        state.attempts = 0U;
        state.fail_at = 0U;
        assert(ip_rate_limiter_init_with_allocator(&limiter, 8U, &policy,
                                                    1000U, &allocator));
        assert(state.attempts == 2U);
        assert(ip_rate_limiter_check(&limiter, UINT32_C(0xc0000201), 1U,
                                     1000U) == IP_RATE_ALLOWED);
        ip_rate_limiter_destroy(&limiter);
        ip_rate_limiter_destroy(&limiter);
        assert_empty(&limiter);
        assert(state.releases == state.successes);
        printf("lifecycle fault %zu: allocation failures recovered, live blocks=0\n",
               fail_point);
    }
}

static void test_invalid_policy_cleans_up(void)
{
    const bucket_policy_t invalid = {0U, 100U};
    ip_rate_limiter_t limiter = {0};
    fault_allocator_state_t state = {0};
    rl_allocator_t allocator = {fault_allocate, fault_deallocate, &state};
    assert(!ip_rate_limiter_init_with_allocator(&limiter, 8U, &invalid,
                                                 1000U, &allocator));
    assert(state.attempts == 2U);
    assert(state.successes == 2U);
    assert(state.releases == 2U);
    assert_empty(&limiter);
}

static void test_custom_allocator_lifetime_and_alignment(void)
{
    const bucket_policy_t policy = {4U, 100U};
    ip_rate_limiter_t limiter = {0};
    fault_allocator_state_t state = {0};
    rl_allocator_t allocator = {fault_allocate, fault_deallocate, &state};
    bool found;
    uint64_t anchor;
    uint16_t tokens;

    assert(ip_rate_limiter_init_with_allocator(&limiter, 64U, &policy,
                                               1000U, &allocator));
    assert(state.attempts == 2U && state.successes == 2U);
    assert(state.maximum_alignment >= _Alignof(lf_bucket_t));
    /* Quiescent lifetime: initialized arrays remain stable during requests. */
    assert(ip_rate_limiter_check(&limiter, 0U, 1U, 1000U) == IP_RATE_ALLOWED);
    assert(ip_rate_limiter_check(&limiter, 0U, 3U, 1000U) == IP_RATE_ALLOWED);
    assert(ip_rate_limiter_check(&limiter, 0U, 1U, 1000U) == IP_RATE_DENIED);
    assert(ip_rate_limiter_snapshot(&limiter, 0U, &found, &anchor, &tokens));
    assert(found && anchor == 1000U && tokens == 0U);
    /* A second init must NOT overwrite a live object or allocate again. */
    assert(!ip_rate_limiter_init(&limiter, 64U, &policy, 1000U));
    assert(state.attempts == 2U);
    ip_rate_limiter_destroy(&limiter);
    assert(state.releases == 2U);
    assert_empty(&limiter);
    assert(ip_rate_limiter_init(&limiter, 8U, &policy, 1000U));
    ip_rate_limiter_destroy(&limiter);
}

int main(void)
{
    test_allocator_contract();
    test_each_failure_is_atomic();
    test_invalid_policy_cleans_up();
    test_custom_allocator_lifetime_and_alignment();
    puts("resource lifecycle tests passed: OOM paths, rollback, retry and cleanup");
    return 0;
}
