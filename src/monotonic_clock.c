#include "monotonic_clock.h"

#include <errno.h>
#include <stdint.h>
#include <time.h>

#define NANOSECONDS_PER_SECOND UINT64_C(1000000000)

int monotonic_clock_timespec_to_ns(const struct timespec *value,
                                    uint64_t *out_ns)
{
    uint64_t sec;
    uint64_t nsec;

    if (value == NULL || out_ns == NULL) {
        errno = EINVAL;
        return -1;
    }

    if (value->tv_sec < 0 || value->tv_nsec < 0 ||
        (uint64_t)value->tv_nsec >= NANOSECONDS_PER_SECOND) {
        errno = EINVAL;
        return -1;
    }

    sec = (uint64_t)value->tv_sec;
    nsec = (uint64_t)value->tv_nsec;

    if (sec > (UINT64_MAX - nsec) / NANOSECONDS_PER_SECOND) {
        errno = ERANGE;
        return -1;
    }

    *out_ns = sec * NANOSECONDS_PER_SECOND + nsec;
    return 0;
}

int monotonic_clock_now_ns(uint64_t *out_ns)
{
    struct timespec sample;

    if (out_ns == NULL) {
        errno = EINVAL;
        return -1;
    }

    if (clock_gettime(CLOCK_MONOTONIC, &sample) != 0) {
        return -1;
    }

    return monotonic_clock_timespec_to_ns(&sample, out_ns);
}
