# Phase 12 - Clock-aware rate-limiter service

## Scope

This phase adds an **in-process request processing API**, not an HTTP listener,
reverse proxy, kernel hook, asynchronous server or persistent IP database.
Callers supply an IPv4 address encoded in `uint32_t` and a positive token cost.
The service chooses the timestamp, calls the Phase 11 per-IP limiter and returns
an explicit decision.

```text
worker -> rate_service_check(service, ipv4, tokens)
               | validate tokens and lifecycle
               v
         immutable clock function + context
               | now_ms from monotonic epoch, or CLOCK_ERROR
               v
         ip_rate_limiter_check(..., now_ms)
               | tagged IPv4 key index, fixed capacity
               v
         preinitialized per-slot atomic token bucket
               | CAS update of packed 48+16-bit state
               v
         ALLOWED / DENIED / TABLE_FULL / INVALID
```

## API and decision semantics

`rate_service_init()` checks that the object is zero-initialized, samples the
injected clock once, validates the 48-bit timestamp and initializes the
preallocated index/buckets. On failure it does not leave a partially usable
service. `rate_service_init_monotonic()` selects the production adapter.

`rate_service_check()` rejects zero-token requests without reading the clock.
For valid calls, it reads the clock **before** any key insertion. Callback
failure or a timestamp exceeding `BUCKET_STATE_MAX_TIME_MS` returns
`RATE_SERVICE_CLOCK_ERROR`, without consuming tokens or creating an entry.
An absent IP first claims an index slot; the bucket associated with that slot
was fully initialized during service initialization. `RATE_SERVICE_DENIED`
means the IP existed or was inserted, but the bucket lacked tokens. The
concurrent key index is fixed-capacity; `RATE_SERVICE_TABLE_FULL` is separate
from rate limiting and leaves all existing buckets usable.

## Milliseconds and clock design

Linux `clock_gettime(CLOCK_MONOTONIC, ...)` feeds the existing adapter
`monotonic_clock_now_ns()`. We divide by 1,000,000 using unsigned integer
arithmetic, rounding **down** to the current complete millisecond. This is a
clock that is unaffected by administrative wall-clock adjustments; it is not
Unix epoch time. Its epoch is unspecified, so do not persist or compare these
timestamps across reboot/machines. `CLOCK_MONOTONIC` can be adjusted gradually
by NTP and commonly does not advance during suspend; stricter timing models
could use `CLOCK_BOOTTIME` on Linux, but that is not supported here.

The packed bucket state allocates 48 bits to `now_ms`, permitting up to
`2^48 - 1` milliseconds since the clock's epoch. Every read is range-checked.
The clock **may be sampled concurrently** by many workers. A test callback
must be thread-safe; the test callback uses C11 atomics for shared flags and
time. The callback is immutable while workers are active.

## Concurrency and progress claims

The Phase 10 hash table uses atomic 64-bit key slots and no concurrent erase;
Phase 11 initializes all bucket payloads before publication. Each bucket's
packed state updates through a single successful CAS in Phase 7, with a
platform lock-free capability check. No malloc/free or mutex appears in the
**built-in request path** after service initialization.

The whole public `rate_service_check()` should **not** be called unconditionally
lock-free: it invokes an injected function pointer (the caller could block).
The built-in monotonic adapter makes a `clock_gettime()` call, whose latency
and guarantees depend on the operating system. The underlying atomic lookup
and bucket state transitions have their own progress/lifetime contracts. No
operation is wait-free under arbitrary contention. The service must not be
reinitialized or destroyed concurrently with requests.

## Tests and reproducibility

`tests/test_rate_service.c` uses an injected atomic millisecond clock to
assert exact boundaries, multi-IP isolation, invalid inputs, clock failures,
full-table distinction, lifecycle, and 12 pthread workers submitting 60,000
requests to two IPs at a fixed timestamp. With bucket capacity 37, exactly
74 should be admitted (37 per IP), and exactly 59,926 denied. A separate
live-clock check samples before and after to bound the adapter's result; it
never sleeps or assumes the precise scheduler delay.

`examples/rate_service_demo.c` offers a deterministic 5-request trace and a
`--live` variant. Live decisions near refill boundaries can vary based on
real elapsed time; do not assert exact live-clock output in CI.

```bash
make clean && make
make rate-service-test
make rate-service-demo
make test
make CC=clang test
```

## Limitations and what comes next

- IPv4 addresses only, passed as 32-bit integers; textual parsing is not in scope.
- Fixed table, no reclamation, erase, eviction, expiry, resizing or IPv6.
- One refill interval and burst capacity shared by all newly allocated IPs.
- A bucket starts full when the service initializes, **not when an IP first arrives**.
  This is acceptable for available burst capacity, but the timestamp choice
  should be documented when switching to other refill semantics.
- Requests are an in-process API; network admission, authentication and
  client-IP trust boundaries are not covered.
- No throughput or latency claims without controlled benchmarks.

Next phases should examine real-world API ergonomics, configuration and
resource exhaustion before claiming any production-readiness.
