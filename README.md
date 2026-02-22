# Lock-Free Per-IP Rate Limiter

A from-scratch systems-programming project evolving into a high-performance,
thread-safe, per-IP token-bucket rate limiter written in C11.

## Project goals

The finished project is intended to demonstrate:

- C11 systems programming and API design
- token-bucket rate limiting
- Linux monotonic time and defensive integer conversion
- atomic operations and memory ordering (C11 teaching lab in Phase 5)
- packed multi-field state-transition design (Phase 6)
- lock-free request-path design (future phases)
- fixed-capacity concurrent hashing (future phases)
- correctness testing and performance analysis (future phases)

## Current status - Phase 6

Phase 6 introduces a **packed state-transition design** for a future concurrent
token bucket: 48 bits of monotonic milliseconds and 16 bits of token balance
inside one `uint64_t`. A pure planner computes the next packed state and the
allow/deny verdict, but does not mutate shared state or perform CAS yet.
Its tests verify 24,000 transitions against the established sequential model.
See `docs/concurrent_bucket_design.md` for explicit limits and the intended
Phase 7 CAS linearization point.

The Phase 5 atomic teaching lab is preserved. The existing sequential
`token_bucket_t` is **not thread-safe**, and the packed-state module is a
**single-threaded design model**, not a production concurrent bucket.
Per-IP routing has not been implemented.

Phase 4's boundary and reference-model testing remain intact.

The bucket starts full, consumes one or more tokens per permitted request, refills
one token per complete configured interval, and never exceeds capacity. Callers
provide `now_ns`, so unit tests can simulate timestamps without sleeping.

The clock module:

- reads `clock_gettime(CLOCK_MONOTONIC, ...)` for elapsed-time accounting;
- converts seconds/nanoseconds into `uint64_t` nanoseconds;
- rejects invalid `timespec` values and conversion overflow;
- returns explicit status and does not overwrite outputs on failure.

`CLOCK_MONOTONIC` values are **not Unix timestamps**. On Linux, this clock normally
does not include time while the system is suspended; that distinction may
matter for future rate-limit policy decisions.

## Requirements

- Linux or WSL2
- GCC or Clang with C11 support
- GNU Make, Bash and Git

On Ubuntu/Debian:

```bash
sudo apt update
sudo apt install -y build-essential git clang gdb valgrind
```

## Build and verify

```bash
make clean
make
make run        # deterministic demonstration (default)
make run-live   # real CLOCK_MONOTONIC sample
make test       # C unit tests, reference-model test + shell smoke test
make properties-test  # 48,000 deterministic reference comparisons
make atomic-test      # CAS contention + release/acquire publication test
make atomic-demo      # small runnable C11 atomics lesson
make state-test       # 24,000 reference-model transition comparisons
make state-demo       # packed state visualized as hex and fields
```

Expected test output includes:

```text
token bucket tests passed
monotonic clock tests passed
token bucket property tests passed: 48000 deterministic operations
atomic lab tests passed: 6 writers, 50000 bounded increments, 6 publication readers
bucket state tests passed: 24000 reference transitions
smoke test passed: deterministic and live clock demos behave as expected
```

You can also test with Clang:

```bash
make clean
make CC=clang
make CC=clang test
```

## Repository layout

```text
.
├── bench/                       # benchmarks arrive later
├── docs/
│   └── concurrent_bucket_design.md # Phase 6 design and planned CAS loop
├── examples/
│   ├── atomic_lab_demo.c        # runnable phase-5 experiments
│   └── bucket_state_demo.c      # Phase 6 packed-state demonstration
├── include/
│   ├── atomic_lab.h             # CAS counter and release/acquire mailbox
│   ├── bucket_state.h           # packed-state planner API
│   ├── monotonic_clock.h        # Linux/POSIX time adapter API
│   └── token_bucket.h           # deterministic bucket API
├── src/
│   ├── atomic_lab.c             # educational C11 atomics module
│   ├── bucket_state.c           # pure packed-state transition model
│   ├── main.c                   # deterministic and live demos
│   ├── monotonic_clock.c        # POSIX clock and safe conversion
│   └── token_bucket.c           # sequential token-bucket policy
├── tests/
│   ├── smoke.sh                 # end-to-end CLI checks
│   ├── test_atomic_lab.c        # concurrent CAS and publication checks
│   ├── test_bucket_state.c      # deterministic state model + reference
│   ├── test_bucket_properties.c # 48,000 model-based operations
│   ├── test_monotonic_clock.c   # pure conversion + monotonicity checks
│   └── test_token_bucket.c      # deterministic policy checks
├── .gitignore
├── Makefile
├── README.md
└── progress.md
```

Learning PDFs are stored **outside** this repository, not committed to Git.

## Design boundary

`monotonic_clock_now_ns()` obtains real time, while `token_bucket_try_consume()`
accepts that time as input. This separation keeps the policy reusable and the
algorithm tests deterministic. Later phases will add per-IP state and concurrency
without forcing real sleeps into the unit tests.

## Sequential bucket contract (Phase 4)

`token_bucket_try_consume_n(&bucket, amount, now_ns)` returns `true` only if
*all* `amount` tokens are available; it never partially deducts them. `amount=0`
is invalid and does not even trigger refill. A failed request for too many tokens
does trigger elapsed-time refill before the rejection. The earlier
`token_bucket_try_consume()` still consumes exactly one token.

Valid initialized state satisfies `0 <= tokens <= capacity`, with positive
capacity and refill interval. Refills cannot move `last_refill_ns` backwards;
any whole-interval anchor advancement is bounded above by the caller's time.
Time values must come from the same monotonic time source.

**Thread-safety warning:** This `token_bucket_t` is still a sequential data
structure. Do not share one instance between threads without synchronization.
A later phase will replace the representation with an atomic concurrency model.


## C11 atomic teaching lab (Phase 5)

`atomic_counter_increment_bounded()` performs a load and CAS retry loop with
`memory_order_relaxed`. It enforces a single numerical invariant (the counter
never exceeds its limit) and **does not** publish other memory. The successful
CAS is its linearization point. A weak CAS can fail spuriously, so the loop
retries using the updated expected value. The counter cannot overflow even if
the limit is `UINT_FAST64_MAX` because `observed + 1` is attempted only when
`observed < limit`.

`atomic_mailbox_publish_once()` writes a plain `uint64_t` payload before a
`memory_order_release` store to `published`. Readers perform a
`memory_order_acquire` load of that flag before reading the payload. The
release/acquire synchronization establishes a happens-before relation for the
payload. The mailbox deliberately supports **one publisher, one publication,
and any number of readers**, after correct initialization. Concurrent second
writes, resetting the ready flag, or destroying the object while readers run
would violate the contract.

`atomic_is_lock_free()` reports whether the atomic counter is lock-free on the
current platform; C11 atomic types are **not** universally guaranteed to be
lock-free. No blanket lock-free project claim should be made yet.

```bash
make atomic-demo
make atomic-test
```

The lab is kept separate from the existing sequential `token_bucket_t` because
atomically updating one numeric counter does **not** yet solve the two-field
(tokens + refill timestamp) consistency problem of a concurrent token bucket.

## Further verification (Linux)

```bash
make clean && make && make test
make clean && make CC=clang && make CC=clang test
make clean && make CFLAGS='-std=c11 -Wall -Wextra -Wpedantic -Werror -O1 -g -fsanitize=undefined -fno-omit-frame-pointer' LDFLAGS='-fsanitize=undefined' test
make clean && make test
```

## Packed state-transition design (Phase 6)

`bucket_state_initial()` starts the 16-bit token balance at capacity.
`bucket_state_plan()` decodes an observed state, advances the refill anchor by
whole intervals without overflow, caps the token balance, and decides whether
the complete request can consume tokens. It returns a **proposed** next state.
Even a rejected request can advance the refill timestamp and balance.

This is **not** a thread-safe API; the planned Phase 7 algorithm will use a
single CAS on the packed state. Millisecond precision intentionally differs
from the existing nanosecond API; requests with sub-millisecond intervals
are currently outside the model. Token balance is limited to 65,535.
`bucket_state_plan()` returns false for malformed inputs and reports ordinary
quota denials via `allowed=false`, avoiding error/deny ambiguity.

```bash
make state-test
make state-demo
```

All phase learning PDFs live **outside** the Git repository.

## Lock-free single-bucket implementation (Phase 7)

`include/lockfree_bucket.h` + `src/lockfree_bucket.c` implement a real
concurrent token bucket by retrying a single-word C11 compare-exchange around
the pure state transition from Phase 6. The logical state (tokens plus refill
anchor) is updated indivisibly. This implementation is **not** yet a per-IP
rate limiter: it manages one shared bucket. `lf_bucket_init()` rejects targets
where its 64-bit atomic state is not reported lock-free.

A successful `lf_bucket_try_consume()` API call returns true and writes a
separate `allowed` decision; false means invalid input, not rate-limit denial.
Only initialization and shutdown may touch the policy/lifetime; both must
occur outside any concurrent worker activity. Workers supply timestamps in
monotonic milliseconds. See `docs/lockfree_bucket_correctness.md` for the
linearization argument, relaxed-memory-order rationale and limitations.

```sh
make lockfree-test   # 800,000 multithreaded requests including batch tests
make lockfree-demo   # print decisions + current coherent snapshots
make test            # includes all previous phases
```

## Phase 8: contention stress and bounded linearizability

Run `make contention-test` (also part of `make test`). The profiling variant
`lf_bucket_try_consume_profiled` reports **CAS attempts**, so retries are
`total_cas_attempts - total_calls`. Denial is a policy decision, not a CAS
failure. Retry counts vary across machines, compilers and scheduler decisions;
never assert a particular number of retries.

The suite repeats an eight-thread 96,000-request stress round ten times
(960,000 requests), checks exact admissions under zero refill, and checks 100
eight-operation concurrent histories (mixed 1000/2000 ms timestamps and
1-/2-token requests) with an exhaustive bounded serial-order
checker. The checker enforces response-before-invocation precedence, matches
request results and the final packed state, and rejects fabricated impossible
histories. This is evidence, **not** a general proof of linearizability or
wait-freedom. The oracle intentionally shares the Phase 6 pure transition model,
so model-level mistakes still require independent specification tests.

For sanitizer experiments see `docs/contention_validation.md`; do not combine
ThreadSanitizer with AddressSanitizer. Tested/verified outcomes should be
recorded honestly on your machine; retry counts are not a performance score.
