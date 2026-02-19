#include "atomic_lab.h"

#include <inttypes.h>
#include <stdio.h>

int main(void)
{
    atomic_counter_t counter;
    atomic_mailbox_t mailbox;
    uint64_t message = 0U;
    unsigned int i;

    if (!atomic_counter_init(&counter, 0U) ||
        !atomic_mailbox_init(&mailbox)) {
        return 1;
    }

    printf("C11 bounded compare-and-swap counter\n");
    for (i = 0U; i < 5U; ++i) {
        printf("attempt %u: %s\n", i + 1U,
               atomic_counter_increment_bounded(&counter, 3U)
                   ? "accepted" : "rejected");
    }
    printf("final count: %" PRIuFAST64 "\n", atomic_counter_get(&counter));
    printf("lock-free on this platform: %s\n",
           atomic_counter_is_lock_free(&counter) ? "yes" : "no");

    /* This sequential example previews publication; concurrent proof is
     * exercised by pthreads in tests/test_atomic_lab.c. */
    if (!atomic_mailbox_try_read(&mailbox, &message)) {
        printf("mailbox before publish: empty\n");
    }
    atomic_mailbox_publish_once(&mailbox, UINT64_C(20261219));
    if (atomic_mailbox_try_read(&mailbox, &message)) {
        printf("mailbox after publish: %" PRIu64 "\n", message);
    }
    return 0;
}
