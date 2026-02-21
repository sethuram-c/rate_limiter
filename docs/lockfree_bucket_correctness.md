# Phase 7: one-word CAS token bucket

The lock-free implementation is intentionally separate from the Phase 4
sequential bucket. Its `lf_bucket_t` contains exactly one mutable logical
state: an `_Atomic uint64_t`. The Phase 6 pure transition model is reused
rather than reimplementing refill arithmetic in the CAS loop.

## Public API and invariants

- `lf_bucket_init()` validates the policy and packs a full initial bucket.
- `lf_bucket_try_consume()` returns *operation validity* as its function
  result, and sets `*allowed` to the quota decision on valid calls.
- `lf_bucket_snapshot()` atomically reads a coherent anchor/token pair. A
  snapshot is not necessarily the state when the caller eventually prints it.
- The 16-bit token field always satisfies `0 <= tokens <= capacity`.
- The 48-bit `anchor_ms` is monotonic under transitions, even for stale time.
- `requested_tokens > 0`; a rejected request never consumes partial tokens.
- No worker may mutate `bucket->policy`, reinitialize the bucket, or free the
  bucket until every concurrent operation has finished.

## Linearization point and C11 memory order

A worker loads the packed 64-bit atomic, uses the pure transition planner,
and tries a weak compare-exchange of the **entire** packed word. On success,
that atomic read/modify/write is its linearization point (including denied
requests). On failure, the expected value is overwritten with the latest
observed state, and the worker recomputes the proposed transition.

`memory_order_relaxed` is sufficient for this particular one-object state
machine: atomic modification order enforces coherent, indivisible updates.
There is no non-atomic mutable payload published by this CAS. The policy is
immutable and must have been *safely published before worker creation*;
thread creation through pthreads is a valid publication boundary. Relaxed
ordering **would not be sufficient** if a future version published additional
non-atomic objects or used the packed state as a general readiness flag.

## Lock-free claim and limitations

`_Atomic uint64_t` is not required by ISO C11 to be lock-free on every
architecture. `lf_bucket_init()` calls `atomic_is_lock_free()` and declines
to initialize a usable object when it reports false. Therefore lock-free
progress is claimed only on a supported platform when init succeeds.
C11 permits spurious failure of `atomic_compare_exchange_weak`, hence the
unbounded retry loop. Under contention, one particular worker may starve;
lock-free means system-wide progress, not per-thread completion guarantees.

No concurrent deletion, hash table, per-IP isolation, resizing, time-source
handling, or dynamic policy change is implemented here. Time is supplied by
the caller in monotonic **milliseconds**, and the packed format caps capacity
at 65,535 and time at `2^48 - 1` ms. This is a CPU-memory primitive, not an
HTTP server or an independently production-ready limiter.

## Exercises and verification

```sh
make clean && make test && make lockfree-demo
make clean && make CC=clang test
make clean && make CFLAGS='-std=c11 -Wall -Wextra -Wpedantic -Werror -O1 -g -fsanitize=undefined -fno-omit-frame-pointer' LDFLAGS='-fsanitize=undefined' test
make clean && make test
```

The contention suite coordinates eight threads with a barrier, then checks
exact success counts across 640,000 one-token calls plus 160,000 three-token
calls. Assertions test no overspending, refill boundaries, stale timestamps,
and unchanged `*allowed` output on invalid input. Such stress tests offer
useful evidence, **not a formal proof** of all possible thread interleavings.

Learning PDFs live outside the repository and must not be committed.
