# Phase 14 - Request diagnostics and retry policy

## Objective

A request can be ALLOWED, DENIED, TABLE_FULL, INVALID or CLOCK_ERROR. These
are distinct operational events. An ordinary quota denial must not be presented
as a memory-capacity failure; a clock failure must not claim an IP slot.

## Atomic decision and snapshot guarantee

`lf_bucket_try_consume_observed()` copies `candidate.next_state` from the **same
successful compare-and-swap** that returned the admission decision. Unlike a
separate `lf_bucket_snapshot()` after the call, this is a coherent *committed*
observation of the operation's linearization point. Another thread may update
the bucket before the result is inspected. It is not a stable live observation.

The higher-level APIs `ip_rate_limiter_check_report()` and
`rate_service_check_detailed()` propagate this exact state to callers; they do
not load the bucket a second time. No locks or dynamic allocation were added
to the fast path. The injected clock callback may have its own behavior and is
not included in the lock-free progress claim.

## Metadata contract

- `result` always describes the decision on a valid `out` pointer.
- `has_bucket_state` is true only for ALLOWED or DENIED.
- `remaining_tokens`, `refill_anchor_ms`, `capacity`, `refill_interval_ms` and
  `sampled_now_ms` are meaningful only if `has_bucket_state` is true.
- A `retry_possible` hint is only meaningful on DENIED. A request larger than
  the configured capacity is never satisfiable; no retry interval is offered.
- If the first sufficient token refill would exceed the packed clock's maximum
  representable timestamp, no retry interval is offered.
- A returned `retry_after_ms` is a *minimum earliest-eligibility estimate* based
  on the sampled clock time, not a guarantee that a retry will be admitted.
  Competing requests may consume tokens in the interim.

## Wait-time equation

If `r` tokens remain after a denied request of size `a`, the deficit is
`d = a - r`, with `1 <= d <= capacity`. Let `A` be the committed refill anchor,
`T` the token refill interval in milliseconds, and `now` the request sample.
The earliest next sufficient refill is at `deadline = A + d*T`. The function
checks the multiplication against `BUCKET_STATE_MAX_TIME_MS - A` before adding.
When feasible, `retry_after_ms = deadline - now`.

This is a **monotonic-clock duration**, not a Unix timestamp. If adapting it to
HTTP `Retry-After`, round **up** to whole seconds (`(ms + 999)/1000`) using an
overflow-safe approach. The in-process C library does not implement HTTP.

## Tradeoffs and limitations

- No atomic snapshot is promised across different IP addresses.
- `RATE_SERVICE_TABLE_FULL` has no per-IP token report because the IP has no slot.
- Caller must use the `has_bucket_state` flag; don't read zeroed fields as
  evidence of a zero-token bucket.
- Existing `rate_service_check()` is retained for callers that want only a
  decision; `rate_service_check_detailed()` is opt-in.
- Bucket policy and key table are immutable/insert-only after initialization.
- Threads must be joined before `rate_service_destroy()`.

## Demonstration and tests

```bash
make rate-diagnostics-test
make rate-diagnostics-demo
make test
```

The tests cover exact admitted balances, fractional interval carryover, a
retry estimate for a batch request, permanently unsatisfiable requests,
clock-failure isolation, full-table handling, packed-time upper bounds and
32,000 concurrent requests across two independently rate-limited IPs.
