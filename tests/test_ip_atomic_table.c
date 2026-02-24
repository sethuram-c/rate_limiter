#include "ip_atomic_table.h"
#include "ip_table_plan.h"

#include <assert.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#define WORKERS 8U
#define DISTINCT_KEYS 128U
#define ROUNDS 200U
#define FULL_WORKERS 24U

typedef struct {
    ip_atomic_table_t *table;
    pthread_barrier_t *barrier;
    unsigned id;
    uint64_t inserted;
    uint64_t existing;
    uint64_t full;
} worker_arg_t;

static void *shared_worker(void *raw)
{
    worker_arg_t *arg = raw;
    size_t round;
    size_t k;
    int rc = pthread_barrier_wait(arg->barrier);
    assert(rc == 0 || rc == PTHREAD_BARRIER_SERIAL_THREAD);
    for (round = 0U; round < ROUNDS; ++round) {
        for (k = 0U; k < DISTINCT_KEYS; ++k) {
            uint32_t ip = (uint32_t)((k + arg->id * 17U + round) % DISTINCT_KEYS);
            ip_atomic_insert_result_t result;
            bool found;
            size_t index;
            assert(ip_atomic_table_get_or_insert(arg->table, ip, &result));
            assert(result.probes >= 1U && result.probes <= arg->table->capacity);
            assert(result.kind != IP_ATOMIC_FULL);
            if (result.kind == IP_ATOMIC_INSERTED) {
                ++arg->inserted;
            } else {
                ++arg->existing;
            }
            assert(ip_atomic_table_find(arg->table, ip, &found, &index));
            assert(found && index == result.index);
        }
    }
    return NULL;
}

static void *full_worker(void *raw)
{
    worker_arg_t *arg = raw;
    ip_atomic_insert_result_t result;
    int rc = pthread_barrier_wait(arg->barrier);
    assert(rc == 0 || rc == PTHREAD_BARRIER_SERIAL_THREAD);
    assert(ip_atomic_table_get_or_insert(arg->table, arg->id, &result));
    if (result.kind == IP_ATOMIC_INSERTED) {
        ++arg->inserted;
    } else if (result.kind == IP_ATOMIC_FULL) {
        ++arg->full;
    } else {
        assert(false && "unique key should never already exist");
    }
    return NULL;
}

static void run_shared_test(void)
{
    ip_atomic_table_t table = {0};
    pthread_barrier_t barrier;
    pthread_t threads[WORKERS];
    worker_arg_t args[WORKERS] = {{0}};
    uint64_t inserted = 0U;
    uint64_t existing = 0U;
    size_t i;

    assert(ip_atomic_table_init(&table, 256U));
    assert(pthread_barrier_init(&barrier, NULL, WORKERS) == 0);
    for (i = 0U; i < WORKERS; ++i) {
        args[i].table = &table;
        args[i].barrier = &barrier;
        args[i].id = (unsigned)i;
        assert(pthread_create(&threads[i], NULL, shared_worker, &args[i]) == 0);
    }
    for (i = 0U; i < WORKERS; ++i) {
        assert(pthread_join(threads[i], NULL) == 0);
        inserted += args[i].inserted;
        existing += args[i].existing;
    }
    assert(pthread_barrier_destroy(&barrier) == 0);
    assert(inserted == DISTINCT_KEYS);
    assert(existing + inserted == WORKERS * DISTINCT_KEYS * ROUNDS);
    assert(ip_atomic_table_count_quiescent(&table) == DISTINCT_KEYS);
    for (i = 0U; i < DISTINCT_KEYS; ++i) {
        bool found;
        size_t index;
        assert(ip_atomic_table_find(&table, (uint32_t)i, &found, &index));
        assert(found && index < table.capacity);
    }
    printf("concurrent dedup: %u operations, exactly %u new IPs\n",
           WORKERS * DISTINCT_KEYS * ROUNDS, DISTINCT_KEYS);
    ip_atomic_table_destroy(&table);
}

static void run_full_test(void)
{
    ip_atomic_table_t table = {0};
    pthread_barrier_t barrier;
    pthread_t threads[FULL_WORKERS];
    worker_arg_t args[FULL_WORKERS] = {{0}};
    uint64_t inserted = 0U;
    uint64_t full = 0U;
    size_t i;

    assert(ip_atomic_table_init(&table, 16U));
    assert(pthread_barrier_init(&barrier, NULL, FULL_WORKERS) == 0);
    for (i = 0U; i < FULL_WORKERS; ++i) {
        args[i].table = &table;
        args[i].barrier = &barrier;
        args[i].id = (unsigned)(i + 1000U);
        assert(pthread_create(&threads[i], NULL, full_worker, &args[i]) == 0);
    }
    for (i = 0U; i < FULL_WORKERS; ++i) {
        assert(pthread_join(threads[i], NULL) == 0);
        inserted += args[i].inserted;
        full += args[i].full;
    }
    assert(pthread_barrier_destroy(&barrier) == 0);
    assert(inserted == 16U && full == 8U);
    assert(ip_atomic_table_count_quiescent(&table) == 16U);
    printf("concurrent saturation: 16 inserted, 8 full\n");
    ip_atomic_table_destroy(&table);
}

static void test_edges(void)
{
    ip_atomic_table_t table = {0};
    ip_atomic_insert_result_t result;
    bool found = true;
    size_t index = 77U;
    uint32_t collisions[3] = {0};
    size_t n = 0U;
    uint32_t ip;

    assert(!ip_atomic_table_init(NULL, 8U));
    assert(!ip_atomic_table_init(&table, 0U));
    assert(!ip_atomic_table_init(&table, 3U));
    assert(!ip_atomic_table_init(&table, SIZE_MAX));
    assert(ip_atomic_table_init(&table, 8U));
    assert(!ip_atomic_table_init(&table, 8U));
    assert(!ip_atomic_table_get_or_insert(NULL, 0U, &result));
    assert(!ip_atomic_table_get_or_insert(&table, 0U, NULL));
    assert(!ip_atomic_table_find(&table, 0U, NULL, &index));
    assert(!ip_atomic_table_find(&table, 0U, &found, NULL));
    assert(found && index == 77U);
    assert(ip_atomic_table_get_or_insert(&table, 0U, &result));
    assert(result.kind == IP_ATOMIC_INSERTED);
    assert(ip_atomic_table_get_or_insert(&table, 0U, &result));
    assert(result.kind == IP_ATOMIC_EXISTS);
    assert(ip_atomic_table_get_or_insert(&table, UINT32_MAX, &result));
    assert(result.kind == IP_ATOMIC_INSERTED);
    assert(ip_atomic_table_find(&table, UINT32_MAX, &found, &index));
    assert(found && index < 8U);
    assert(ip_atomic_table_find(&table, UINT32_C(0xabcdef01), &found, &index));
    assert(!found && index == 8U);

    /* Force three distinct keys through the same hash home slot. */
    for (ip = 1U; n < 3U; ++ip) {
        if ((ip_table_plan_hash(ip) & 7U) == 3U) {
            collisions[n++] = ip;
        }
    }
    for (n = 0U; n < 3U; ++n) {
        assert(ip_atomic_table_get_or_insert(&table, collisions[n], &result));
        assert(result.kind == IP_ATOMIC_INSERTED);
    }
    assert(ip_atomic_table_count_quiescent(&table) == 5U);
    for (n = 0U; n < 3U; ++n) {
        assert(ip_atomic_table_find(&table, collisions[n], &found, &index));
        assert(found && index < 8U);
    }
    ip_atomic_table_destroy(&table);
    assert(table.slots == NULL && table.capacity == 0U);
    ip_atomic_table_destroy(&table);
    assert(ip_atomic_table_init(&table, 1U));
    assert(ip_atomic_table_get_or_insert(&table, UINT32_MAX, &result));
    assert(result.kind == IP_ATOMIC_INSERTED);
    assert(ip_atomic_table_get_or_insert(&table, 0U, &result));
    assert(result.kind == IP_ATOMIC_FULL && result.index == 1U);
    assert(ip_atomic_table_get_or_insert(&table, UINT32_MAX, &result));
    assert(result.kind == IP_ATOMIC_EXISTS);
    ip_atomic_table_destroy(&table);
}

int main(void)
{
    test_edges();
    run_shared_test();
    run_full_test();
    puts("atomic IP table tests passed");
    return 0;
}
