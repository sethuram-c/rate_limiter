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
- lock-free single-token-bucket CAS request processing (Phase 7)
- fixed-capacity concurrent IPv4 hashing (Phase 10)
- correctness testing and performance analysis (future phases)

## Current status - Phase 12

Phase 12 adds a **clock-aware in-process request API** (`rate_service_t`)
above the Phase 11 fixed-capacity, per-IPv4 rate limiter. `rate_service_check()`
samples a Linux monotonic clock and returns explicit `ALLOWED`, `DENIED`,
`TABLE_FULL`, `INVALID` or `CLOCK_ERROR` results. A dependency-injected clock
allows precise, deterministic tests without sleeps. Every IP key selects a
preinitialized, independently CAS-updated token bucket.

```bash
make rate-service-test            # targeted clock-aware API tests
make rate-service-demo            # deterministic trace + live-clock trace
./build/rate_service_demo --live  # real CLOCK_MONOTONIC mode
make test                         # full suite through Phase 12
```

The public interface is currently an **in-process library**, not a network
server. The underlying bucket/key atomics are checked for lock-free support;
caller-provided clock callbacks can themselves block. See
`docs/live_request_api.md` for initialization, lifetime, concurrency,
clock-source assumptions and limitations.

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
make lockfree-test    # Phase 7 concurrent CAS bucket tests
make contention-test  # Phase 8 CAS contention and bounded history checks
make ip-plan-test     # Phase 9 sequential per-IP hash-table model
make ip-plan-demo     # Phase 9 probe/collision demonstration
make ip-atomic-test    # Phase 10 concurrent insertion/lookup tests
make ip-atomic-demo    # Phase 10 tagged key/atomic publication demo
make ip-rate-test       # Phase 11 per-IP bucket integration
make ip-rate-demo       # Phase 11 per-IP demonstration
make rate-service-test  # Phase 12 clock-aware API
make rate-service-demo  # Phase 12 deterministic + live demos
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

## Phase 9: fixed-capacity IPv4 index reference model

`include/ip_table_plan.h` defines the non-concurrent semantics we will preserve
when introducing atomic slots: consistent hashing, linear probing, first-empty
termination, duplicate-key idempotence, wraparound, and a full-table signal.
The model holds an explicit `occupied` flag, so **all 2^32 IPv4 keys are valid**.
Capacity must be nonzero and a power of two; a full scan is bounded to
`capacity` inspected slots. Keys are canonical network addresses represented
as `uint32_t`, not raw string or host-specific pointer identities.

The model is intentionally **not** presented as lock-free. A future concurrent
implementation cannot safely publish a key before completing the associated
bucket initialization unless it has a correct reservation/helping protocol.

```sh
make ip-plan-test   # 60,000 deterministic operations against an independent set
make ip-plan-demo   # shows inserts, duplicates and slot count
make test           # all prior tests plus Phase 9
```

## Phase 10: concurrent IPv4 key index

```bash
make ip-atomic-test
make ip-atomic-demo
make test
```

The pthread stress test executes 204,800 insert-or-find calls over 128
unique keys and ensures exactly one successful insertion per IP. A concurrent
full-table test admits exactly 16 unique keys to a 16-slot table and reports
FULL for the other eight. This is a bounded concurrent key index, not yet a
per-IP rate-limiter admission function.

## Phase 11: concurrent per-IP admission

The project now includes a complete **library-level per-IPv4 rate limiter**:
`ip_rate_limiter_t` preallocates and initializes one `lf_bucket_t` per atomic
hash-table slot, then lets concurrent workers claim stable IP-to-slot mappings.
One IP cannot consume another IP's quota; new and existing IPs use the same
atomic bucket CAS path.

```sh
make ip-rate-test
make ip-rate-demo
make test
```

Public API: `include/ip_rate_limiter.h`. Result categories are `IP_RATE_ALLOWED`,
`IP_RATE_DENIED`, `IP_RATE_TABLE_FULL` and `IP_RATE_INVALID` - never conflate
quota denial with table exhaustion. No request-path allocation occurs. All
bucket payloads are ready before any IP slot becomes visible. An example:

```c
const bucket_policy_t policy = {2U, 100U};
ip_rate_limiter_t limiter = {0};
if (!ip_rate_limiter_init(&limiter, 128U, &policy, 1000U)) {
    /* initialization failed: stop or fall back */
}
/* Only if init succeeded: run concurrent requests with monotonic ms. */
/* ip_rate_limiter_check(&limiter, ip, 1U, now_ms); */
/* After joining all threads: ip_rate_limiter_destroy(&limiter); */
```

This module is not yet an HTTP server, network filter, persistent store or
production-hardened service. It supports fixed power-of-two table capacity,
insert-only entries, global policy and a quiescent destruction contract. See
`docs/per_ip_integration.md` for the publication, linearization and lifetime
argument. Phase learning PDFs remain outside the repo.

## Phase 12 learning objectives

The new boundary is clock awareness without losing deterministic tests. Learn
why checking clock failure before insertion preserves the request contract;
why clocks are injected and callbacks need their own thread-safety guarantee;
and why a lock-free atomic core does not automatically guarantee a lock-free
entire request processing function. Store personal phase PDFs outside Git.
