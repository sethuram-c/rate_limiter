# Phase 15 - Operational status and policy counters

## Scope

This phase adds *observability* to the existing in-process rate service. It is
not an HTTP gateway, Prometheus exporter, shared-memory counter service or
replacement for the per-IP token bucket. No allocations or mutexes were added
to the request path.

## Distinct request outcomes

| Raw result | Operational category | Suggested caller behavior |
| --- | --- | --- |
| `RATE_SERVICE_ALLOWED` | `RATE_STATUS_ADMIT` | Continue request processing |
| `RATE_SERVICE_DENIED` | `RATE_STATUS_THROTTLED` | Reject/throttle this request; optional retry hint via Phase 14 diagnostic |
| `RATE_SERVICE_TABLE_FULL` | `RATE_STATUS_CAPACITY_EXHAUSTED` | Capacity failure; do not mistake it for a token-quota denial |
| `RATE_SERVICE_INVALID` | `RATE_STATUS_BAD_REQUEST` | Fix caller arguments; do not retry blindly |
| `RATE_SERVICE_CLOCK_ERROR` | `RATE_STATUS_CLOCK_UNAVAILABLE` | Investigate the clock dependency, avoid taking a slot |

The categories are application-level, not automatic HTTP response mappings.
A caller must decide its own HTTP/gRPC status codes and retry strategy.

## Counter semantics

`rate_service_get_stats()` reads five independent `uint64_t` atomic counters.
Both request APIs increment **one and only one** corresponding counter for
every completed request on an initialized service. The increment is performed
*after* the underlying decision and, for detailed calls, after writing the
report. A valid clock read does not guarantee an admission.

Increments and loads use `memory_order_relaxed`. Atomic operations prevent
counter data races and lost increments. They are not used to publish other
thread data, because only counts are being communicated. At initialization,
all five counter objects undergo a lock-free capability check, rejecting
platforms that cannot update them lock-free. Cumulative values are modulo
2^64 if exhausted.

Snapshot caveat: while other threads are checking requests, each counter load
may occur at a different instant. The snapshot is NOT a coherent global point
in time: the sum may omit in-flight requests and may differ from concurrently
observed totals. **After joining all workers**, exact final equality holds:
`allowed + denied + table_full + invalid + clock_error = completed requests`
provided no wraparound occurred.

`rate_service_check_detailed(..., NULL)` does not attempt a request. If the
service itself is NULL or has not been initialized, no valid counter bank
exists, so this invalid return cannot be counted. Initialization failures are
not request outcomes. `rate_service_destroy()` requires quiescence and makes
snapshots unavailable until reinitialization. Counters start at zero on
successful initialization; there is deliberately no concurrent reset API.

## Verify on Linux

```bash
make clean
make
make rate-status-test
make rate-status-demo
make test

# Alternative compiler
make clean && make CC=clang test

# UndefinedBehaviorSanitizer (targeted)
make clean
make rate-status-test CFLAGS='-std=c11 -Wall -Wextra -Wpedantic -Werror -O1 -g -fsanitize=undefined -fno-omit-frame-pointer' LDFLAGS='-fsanitize=undefined'
```

## Concurrency and progress caveats

The request function still invokes an application-supplied clock callback,
which may block. Lock-free is the claim for the atomic index/bucket/counter
operations on platforms passing their capability checks, not a guarantee that
an entire application request has a nonblocking clock provider. Counters are
per-service aggregates, not per-IP statistics. A high-frequency global counter
may cause cache-line contention; benchmark it before claiming performance.
