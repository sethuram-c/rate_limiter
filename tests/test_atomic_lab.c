#include "atomic_lab.h"

#include <assert.h>
#include <pthread.h>
#include <sched.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#define WORKERS 6U
#define ATTEMPTS 20000U
#define BOUND 50000U
#define READERS 6U
#define MESSAGE UINT64_C(0xDEADBEEF12345678)

typedef struct {
    atomic_counter_t *counter;
    uint_fast64_t successful;
} counter_work_t;

static void *counter_worker(void *arg)
{
    counter_work_t *work = arg;
    unsigned int attempt;

    for (attempt = 0U; attempt < ATTEMPTS; ++attempt) {
        if (atomic_counter_increment_bounded(work->counter, BOUND)) {
            ++work->successful; /* Private to this worker until join. */
        }
    }
    return NULL;
}

static void test_counter_boundaries(void)
{
    atomic_counter_t counter;
    unsigned int n;

    assert(!atomic_counter_init(NULL, 0U));
    assert(!atomic_counter_increment_bounded(NULL, 1U));
    assert(atomic_counter_init(&counter, 0U));
    assert(!atomic_counter_increment_bounded(&counter, 0U));
    for (n = 0U; n < 5U; ++n) {
        assert(atomic_counter_increment_bounded(&counter, 5U));
    }
    assert(!atomic_counter_increment_bounded(&counter, 5U));
    assert(atomic_counter_get(&counter) == 5U);

    assert(atomic_counter_init(&counter, UINT_FAST64_MAX - 1U));
    assert(atomic_counter_increment_bounded(&counter, UINT_FAST64_MAX));
    assert(!atomic_counter_increment_bounded(&counter, UINT_FAST64_MAX));
    assert(atomic_counter_get(&counter) == UINT_FAST64_MAX);
}

static void test_counter_contention(void)
{
    atomic_counter_t counter;
    pthread_t threads[WORKERS];
    counter_work_t work[WORKERS];
    uint_fast64_t successful = 0U;
    unsigned int i;

    assert(atomic_counter_init(&counter, 0U));
    for (i = 0U; i < WORKERS; ++i) {
        work[i].counter = &counter;
        work[i].successful = 0U;
        assert(pthread_create(&threads[i], NULL, counter_worker, &work[i]) == 0);
    }
    for (i = 0U; i < WORKERS; ++i) {
        assert(pthread_join(threads[i], NULL) == 0);
        successful += work[i].successful;
    }
    assert(successful == BOUND);
    assert(atomic_counter_get(&counter) == BOUND);
}

typedef struct {
    const atomic_mailbox_t *mailbox;
    uint64_t received;
} reader_work_t;

static void *reader_worker(void *arg)
{
    reader_work_t *work = arg;
    uint64_t value = 0U;

    while (!atomic_mailbox_try_read(work->mailbox, &value)) {
        sched_yield(); /* Just for the test; not a production wait strategy. */
    }
    work->received = value;
    return NULL;
}

static void test_publication(void)
{
    atomic_mailbox_t mailbox;
    pthread_t threads[READERS];
    reader_work_t work[READERS];
    uint64_t unchanged = 17U;
    unsigned int i;

    assert(!atomic_mailbox_init(NULL));
    assert(atomic_mailbox_init(&mailbox));
    assert(!atomic_mailbox_try_read(&mailbox, &unchanged));
    assert(unchanged == 17U);
    assert(!atomic_mailbox_try_read(&mailbox, NULL));
    assert(!atomic_mailbox_try_read(NULL, &unchanged));

    for (i = 0U; i < READERS; ++i) {
        work[i].mailbox = &mailbox;
        work[i].received = 0U;
        assert(pthread_create(&threads[i], NULL, reader_worker, &work[i]) == 0);
    }
    /* Exactly one publisher; concurrent readers observe its entire payload. */
    atomic_mailbox_publish_once(&mailbox, MESSAGE);
    for (i = 0U; i < READERS; ++i) {
        assert(pthread_join(threads[i], NULL) == 0);
        assert(work[i].received == MESSAGE);
    }
    assert(atomic_mailbox_try_read(&mailbox, &unchanged));
    assert(unchanged == MESSAGE);
}

int main(void)
{
    atomic_counter_t counter;

    test_counter_boundaries();
    test_counter_contention();
    test_publication();
    assert(atomic_counter_init(&counter, 0U));
    printf("atomic lab tests passed: 6 writers, %u bounded increments, "
           "6 publication readers\n", BOUND);
    printf("atomic counter lock-free on this platform: %s\n",
           atomic_counter_is_lock_free(&counter) ? "yes" : "no");
    return 0;
}
