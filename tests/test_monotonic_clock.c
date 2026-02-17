#include "monotonic_clock.h"

#include <assert.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <time.h>

#define NS_PER_SEC UINT64_C(1000000000)

static void test_converts_valid_timespec_values(void)
{
    const struct timespec origin = { .tv_sec = 0, .tv_nsec = 0 };
    const struct timespec seconds = { .tv_sec = 12, .tv_nsec = 345678901L };
    const struct timespec edge = { .tv_sec = 1, .tv_nsec = 999999999L };
    uint64_t result = UINT64_MAX;

    assert(monotonic_clock_timespec_to_ns(&origin, &result) == 0);
    assert(result == 0U);

    assert(monotonic_clock_timespec_to_ns(&seconds, &result) == 0);
    assert(result == 12U * NS_PER_SEC + UINT64_C(345678901));

    assert(monotonic_clock_timespec_to_ns(&edge, &result) == 0);
    assert(result == 2U * NS_PER_SEC - 1U);
}

static void test_rejects_invalid_timespec_values(void)
{
    const struct timespec negative_sec = { .tv_sec = -1, .tv_nsec = 0 };
    const struct timespec negative_nsec = { .tv_sec = 0, .tv_nsec = -1 };
    const struct timespec too_many_nsec = { .tv_sec = 0, .tv_nsec = 1000000000L };
    uint64_t result = UINT64_C(12345);

    errno = 0;
    assert(monotonic_clock_timespec_to_ns(NULL, &result) == -1);
    assert(errno == EINVAL && result == UINT64_C(12345));

    errno = 0;
    assert(monotonic_clock_timespec_to_ns(&negative_sec, &result) == -1);
    assert(errno == EINVAL && result == UINT64_C(12345));

    errno = 0;
    assert(monotonic_clock_timespec_to_ns(&negative_nsec, &result) == -1);
    assert(errno == EINVAL && result == UINT64_C(12345));

    errno = 0;
    assert(monotonic_clock_timespec_to_ns(&too_many_nsec, &result) == -1);
    assert(errno == EINVAL && result == UINT64_C(12345));

    errno = 0;
    assert(monotonic_clock_timespec_to_ns(&too_many_nsec, NULL) == -1);
    assert(errno == EINVAL);
}

static void test_rejects_overflow_when_time_t_supports_large_seconds(void)
{
    /* A 32-bit time_t cannot represent the seconds needed for this case. */
    if (sizeof(time_t) >= sizeof(uint64_t)) {
        const struct timespec overflow = {
            .tv_sec = (time_t)(UINT64_MAX / NS_PER_SEC + UINT64_C(1)),
            .tv_nsec = 0
        };
        uint64_t result = UINT64_C(67890);

        errno = 0;
        assert(monotonic_clock_timespec_to_ns(&overflow, &result) == -1);
        assert(errno == ERANGE);
        assert(result == UINT64_C(67890));
    }
}

static void test_reads_monotonic_clock(void)
{
    uint64_t previous;
    uint64_t current;
    unsigned int i;

    assert(monotonic_clock_now_ns(&previous) == 0);

    /* Monotonic means nondecreasing, not strictly increasing. */
    for (i = 0U; i < 1000U; ++i) {
        assert(monotonic_clock_now_ns(&current) == 0);
        assert(current >= previous);
        previous = current;
    }
}

static void test_rejects_null_output(void)
{
    errno = 0;
    assert(monotonic_clock_now_ns(NULL) == -1);
    assert(errno == EINVAL);
}

int main(void)
{
    test_converts_valid_timespec_values();
    test_rejects_invalid_timespec_values();
    test_rejects_overflow_when_time_t_supports_large_seconds();
    test_reads_monotonic_clock();
    test_rejects_null_output();

    puts("monotonic clock tests passed");
    return 0;
}
