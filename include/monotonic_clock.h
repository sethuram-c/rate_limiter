#ifndef MONOTONIC_CLOCK_H
#define MONOTONIC_CLOCK_H

#include <stdint.h>
#include <time.h>

/*
 * Convert a valid POSIX timespec to nanoseconds without integer overflow.
 * Returns 0 on success, or -1 with errno set to EINVAL or ERANGE.
 * On failure, *out_ns is unchanged when out_ns is non-NULL.
 * Exposed separately so the conversion can be unit-tested deterministically.
 */
int monotonic_clock_timespec_to_ns(const struct timespec *value,
                                    uint64_t *out_ns);

/*
 * Read CLOCK_MONOTONIC, then convert its value to nanoseconds.
 * Returns 0 on success, or -1 with errno set by the failed operation.
 * The clock is monotonic, not a civil/wall-clock timestamp.
 * On failure, *out_ns is unchanged when out_ns is non-NULL.
 */
int monotonic_clock_now_ns(uint64_t *out_ns);

#endif
