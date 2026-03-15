# Lock-Free Per-IP Rate Limiter

## Phase 32 - Architecture review and deduplicated IPv4 parsing

Gateway, CIDR parser and policy matcher now share a private, bounded IPv4
normalization helper in `src/ipv4_parse_internal.h`. Public headers, the
static-library API, and the lock-free admission algorithm are unchanged.
`make ipv4-internal-test` covers the shared parser; full ingress, policy and
installed-consumer tests guard the existing contracts. See
[maintainability review](docs/architecture_maintainability.md).
The separate Phase 32 learning PDF is not part of this Git repository.

A from-scratch C11 systems project implementing an in-process, per-IP token-bucket rate limiter.

## Public API reference (Phase 31)

- [Public API reference](docs/public_api_reference.md) explains the entry points, enum outcomes, ownership, threading, clock, CIDR policy, and diagnostic/counter contracts.
- [Developer integration guide](docs/developer_integration_guide.md) provides an external installed-header compilation path and Fedora/Linux troubleshooting.
- `examples/public_api_walkthrough.c` is a reproducible staged-library consumer exercising version, budget, bounded IPv4 parsing, 200/400/403/429 policy paths, refill and clean teardown.
- `make docs-test` verifies the documented symbols and links, stages and links the consumer against **installed** headers and the static library, and checks expected output. It is also included in `make test`.
- The project remains an **in-process C11 library**, version `0.1.0` Unreleased; no HTTP listener or stable ABI is promised. Personal study PDFs are stored outside this repository.

## Phase 29 - Reusable static library and standalone integration

The project can now be built as `build/libratelimiter.a`, installed under a
staging prefix, and linked into an **independent C11 consumer** using only
installed headers. The recommended external include is
`#include <ratelimiter/ratelimiter.h>`.

```bash
make library
make package-test
make install DESTDIR="$PWD/build/stage" PREFIX=/usr
```

This does not imply shared-library/ABI stability or a network service.
See `docs/library_packaging.md` for link order, POSIX flags, thread-lifetime
contracts and scope of the packaged implementation. The separate Phase 29
study PDF is intentionally not tracked in Git.

## Phase 28 - Length-bounded ingress and API contract tests

Network-facing request buffers are often views rather than NUL-terminated
strings. `request_gateway_handle_n(service, bytes, length, tokens, &response)`
now parses an explicitly bounded 1..15-byte IPv4 view without reading past
`length`, while the legacy `request_gateway_handle()` stays available.
Malformed/embedded-NUL/oversized buffers are rejected before clock or limiter
state changes. Fresh unit tests validate all outcomes, including 32,000
concurrent requests across two IPs (74 allowed at fixed time).

```bash
make api-contract-test
make api-contract-asan
make ci-config-test
make test
```

Read `docs/api_contract_hardening.md` for lifetime, failure-path and length
preconditions. Keep the personal Phase 28 PDF out of Git.

## Project goals

The finished project is intended to demonstrate:

- C11 systems programming and API design
- token-bucket rate limiting
- Linux monotonic time and defensive integer conversion
- atomic operations and memory ordering (C11 teaching lab in Phase 5)
- packed multi-field state-transition design (Phase 6)
- lock-free single-token-bucket CAS request processing (Phase 7)
- fixed-capacity concurrent IPv4 hashing (Phase 10)
- correctness testing and performance analysis (Phases 18-23)

## Historical milestone - Phase 16

Earlier Phase 13 added **explicit configuration planning, capacity bounds and a
resource-budgeted initializer** on top of the Phase 12 clock-aware API.
`rate_config_plan()` validates power-of-two table size, the 16-bit maximum
token capacity, 48-bit clock limit, overflow-safe allocation arithmetic and a
caller-specified **payload-array memory budget** before any side effects.
`rate_service_init_with_config()` validates before sampling the clock.
The original initialization APIs remain available for compatibility.

```bash
make rate-config-test             # invalid/boundary/overflow/memory tests
make rate-config-demo             # actual byte estimate + rejection example
make rate-service-test            # Phase 12 clock-aware API tests
make test                         # full suite through Phase 13
```

The rate limiter remains an **in-process library**, not a network server.
Memory budgeting covers the two primary array payloads, **not** malloc
metadata, stacks, or process RSS. The underlying atomic primitives check
lock-free support, but user clocks may block. Read
`docs/configuration_limits.md` for exact safety and lifetime contracts.

## Phase 15 - Operational status and request outcome counters

The Phase 15 service adds five independent C11 relaxed-atomic outcome
counters, covering ALLOWED, DENIED, TABLE_FULL, INVALID, CLOCK_ERROR. Both
legacy and detailed request APIs count each completed initialized-service
request exactly once. `rate_service_get_stats()` reads the counters without
mutating them. `rate_service_classify()` and `rate_service_result_name()`
map raw results to stable categories and labels.

```bash
make rate-status-test
make rate-status-demo
make test
```

**Concurrency caveat:** During worker activity, the five counter loads do not
represent a consistent global instant. Take a final snapshot after workers
join to assert totals. Counters accumulate until quiescent destruction and
reinitialization, and wrap modulo 2^64 if exhausted. An invalid request with
no initialized service cannot be counted, nor can a detailed call with a
NULL output pointer (no attempt is made). Read `docs/operational_status.md`.

## Phase 16 - In-process HTTP-style gateway and request simulation

A tiny application adapter accepts validated dotted-decimal IPv4 client text,
converts it to a numeric key, invokes the existing detailed rate-service API,
and maps outcomes to illustrative HTTP-style statuses (200, 429, 400, 503).
It includes decision-aligned remaining tokens, optional earliest-retry hints,
and an explicit flag for requests rejected **before** reaching the service.

```bash
make request-gateway-test  # strict IPv4 input, timeline and concurrency
make request-gateway-demo  # 11-event deterministic multi-IP trace
make test                  # all prior phases + Phase 16
```

The trace has 11 ingress events but only 10 service calls because one malformed
IP is rejected locally. Final service counts: 5 allowed, 2 denied, 1 full,
1 invalid, 1 clock error. The concurrent test makes 40,000 calls to four IPs
with 37 tokens each and checks exactly 148 admissions.

**Not an HTTP server:** the adapter does not bind sockets, trust forwarding
headers or serialize HTTP responses. Real deployments need authenticated
client identity at the networking boundary. See `docs/request_gateway_integration.md`.

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
make rate-config-test   # Phase 13 defensive configuration tests
make rate-config-demo   # Phase 13 allocation planning walkthrough
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


## Phase 14 - Decision-aligned request diagnostics

`rate_service_check_detailed()` returns a result plus a token balance captured
from the very same successful bucket CAS. The report is **not** a live snapshot;
other workers can change the bucket afterward. Denied requests with achievable
refill deadlines receive a monotonic-millisecond `retry_after_ms` hint.
Requests exceeding capacity, clock errors, and table-full decisions do not.
See [`docs/request_diagnostics.md`](docs/request_diagnostics.md).

```sh
make rate-diagnostics-test
make rate-diagnostics-demo
make test
```

## Phase 17: static application policy (IPv4 CIDR)

`request_policy_t` adds up to 32 rules over the existing gateway, with strict
CIDR parsing, longest-prefix matching, and BLOCK winning same-prefix ties.
BYPASS returns HTTP-style 200 without token/counter changes, BLOCK returns 403
without token/counter changes, and unmatched traffic takes the Phase 16 path.
A zero-cost request cannot be exempted from validation. Configure the rules
**before** starting worker threads and never mutate them while requests run.

```sh
make request-policy-test
make request-policy-demo
make test
```

See [`docs/request_policy_configuration.md`](docs/request_policy_configuration.md)
for precedence, security assumptions, accounting, and concurrency contracts.
This is still an **in-process** application adapter, not a deployed HTTP server.

## Phase 18: reproducible workloads

Run `make benchmark-smoke` for accounting checks or `make workload-bench` for sample throughput. See `docs/benchmark_methodology.md`; this is an in-process microbenchmark, not HTTP throughput.

## Phase 19: repeated runs and thread scaling

Use `make scaling-test` for the stdlib-only analysis tests and `make scaling-report`
for repeated fixed-total-request runs at 1/2/4/8 threads. CSV output is written
to `build/scaling_raw.csv` and `build/scaling_summary.csv` (ignored by Git).
The report shows median, percentile **run-throughput** ranges, MAD, speedup,
and efficiency, with correctness checks on each sample. The measurements include
pthread creation and joining, are not network or latency measurements, and do
not establish a comparison against a mutex implementation. For methodology and
Linux tuning commands see [`docs/scaling_analysis.md`](docs/scaling_analysis.md).


## Phase 20: mutex bucket reference and controlled A/B comparisons

The repository now provides a pthread-mutex bucket baseline over **the same
atomic IP index and same pure transition function** as the lock-free version.
It isolates synchronization strategy rather than comparing unrelated maps.
Tests verify deterministic equivalence, concurrent aggregate admissions and
explicit input/table-full behavior before any performance measurement.

```sh
make mutex-test
make mutex-compare-test
make mutex-compare-sample
make mutex-compare-report
```

The comparison report exports paired raw samples and median summaries to
ignored `build/` CSV files. A ratio greater than one means lock-free was faster
for those particular samples; no universal speedup is claimed. The timed
section differs from Phase 18's thread-startup-inclusive benchmark. See
[`docs/mutex_comparison.md`](docs/mutex_comparison.md) for exact methodology,
limitations, profiling commands and interpretation. The personal learning PDF
remains outside the repository.

## Phase 21: realistic synthetic traffic and bottleneck analysis

The `realistic_traffic_bench` driver evaluates the mutex and CAS buckets on
identical seeded hot, uniform and skewed request streams, with unit or mixed
request costs. It validates **per-IP token budgets** after joining threads,
not just request counts, and writes repeated paired results to ignored CSVs.

```sh
make traffic-profile-test
make traffic-profile-sample
make traffic-profile-report
./build/realistic_traffic_bench lockfree 8 100000 skewed 64 mixed 20270104
```

See [`docs/traffic_bottlenecks.md`](docs/traffic_bottlenecks.md) for the
benchmark contract, profiling commands, limitations, and analysis. The timed
metric is attempted in-memory requests per second, not HTTP RPS; results
from a single run must not be used as a resume performance claim.

## Phase 22: optional cache-line-isolated buckets

A second **build-time** layout isolates the atomic state of each lock-free
per-IP bucket on an assumed 64-byte cache line. The original compact layout
remains the default; compile the entire project consistently with
`-DRL_CACHELINE_PADDED` to select the padded ABI.

```sh
make cacheline-test
make cacheline-sample
make cacheline-report
```

The two benchmark binaries measure identical seeded traffic (compact vs
padded), write ignored CSV samples and validate per-IP token conservation.
Padding costs additional memory and was not consistently faster in the
measured environment. See [docs/cacheline_experiment.md](docs/cacheline_experiment.md)
for correctness, experimental controls, profiler commands and limitations.
Keep the personal Phase 22 learning PDF outside the repository.

### Phase 23 latency analysis
Run `make latency-test` and `make latency-sample` to inspect per-operation p50/p95/p99; see `docs/latency_analysis.md`.

## Phase 24 - Allocation failure safety and lifecycle

The project now offers optional deterministic allocation failure injection
via `rl_allocator_t`, `ip_atomic_table_init_with_allocator()` and
`ip_rate_limiter_init_with_allocator()`. The default APIs remain unchanged.
The allocator callbacks are invoked only during initialization and
**quiescent** destruction. They are never invoked by request processing.

```bash
make lifecycle-test
make lifecycle-test-padded
```

The focused tests force failure of the first and second allocation, assert
rollback with no live blocks, check retry after failure, double destruction,
and custom allocator alignment. See `docs/resource_lifecycle.md`.

**Lifetime constraint:** never free, move, reconfigure, or destroy a limiter
until all user threads have stopped and joined.

## Phase 25 - Concurrency audit and sanitizer diagnostics

The focused audit exercises 160,000 fixed-time concurrent requests against 16
IP addresses and verifies exact admission/invalid/denial totals, per-IP tokens,
atomic service counters and quiescent teardown. Compact and padded layouts are
both tested. Sanitizer checks do not replace review of memory ordering and
object lifetime.

```sh
make audit-test audit-test-padded audit-runner-test
make audit-asan  # AddressSanitizer + UndefinedBehaviorSanitizer, instrumented
make audit-tsan  # ThreadSanitizer; returns 77 if runtime is unsupported
make benchmark-cli-test  # previously untracked Phase 18 CLI benchmark sources
make benchmark-cli-sample
```

`bench/benchmark_rate_service.c` and `tests/test_benchmark.sh` are now tracked
by Git and integrated into the Makefile. Keep all generated cache directories,
CSV results and private learning PDFs outside version control. See
[docs/concurrency_safety_audit.md](docs/concurrency_safety_audit.md) for the
contracts, expected results, sanitizer statuses and audit checklist.

## Phase 26 - Continuous integration

GitHub Actions builds and tests this project automatically on pushes, pull
requests and manual runs with a GCC/Clang matrix, plus a separate ASan/UBSan
concurrency-audit job. CI uses least-privilege `contents: read` permissions.

```bash
make ci-config-test       # stdlib checks for required workflow configuration
make CC=gcc test          # full local suite
make clean && make CC=clang test  # independent Clang suite
make CC=gcc audit-asan    # focused memory/UB sanitizer audit
```

See `docs/continuous_integration.md` for Fedora setup, remote activation,
limitations, and how to diagnose a failed job. Local checks are not proof
that hosted Actions have run: inspect the repository Actions tab after push.

## Phase 27 - Static analysis and failure-path code quality

Phase 27 adds compiler-native static analysis alongside existing runtime
and sanitizer testing. GCC analyzes every C translation unit; Clang analyzes
production source and the latency benchmark where analysis discovered an
allocation leak on initializer failure. Both initializer-failure branches
now use centralized cleanup rather than returning with `all_samples` allocated.

```bash
make static-runner-test
make static-check       # GCC -fanalyzer + Clang Static Analyzer
make latency-test       # verify fixed benchmark still works
make ci-config-test     # includes required static-analysis job
make test
```

See `docs/static_analysis_review.md` for file coverage, Fedora setup,
limitation notes and the engineering review checklist. Static analysis is
not a substitute for sanitizer runs or concurrency correctness proofs.

## Phase 30 - Source versioning and release-readiness preflight

Development milestone **0.1.0** is recorded in `VERSION`,
`include/rate_version.h` and `CHANGELOG.md`. The installed static archive
exposes `rl_version_string()` so consumers can check compiled headers against
the linked implementation. This milestone is **unreleased** and pre-1.0:
public struct layouts are visible and there is **no stable ABI promise**.

```sh
make version-test
make release-contract-test
make release-check    # staged install, CI contract, static-analysis preflight
make test             # full regression including version tests
```

A real release requires a license/distribution decision, successful hosted CI,
a deliberate Git tag and explicit publication. None occurs automatically.
See [docs/release_readiness.md](docs/release_readiness.md) for the detailed
checklist. Keep the Phase 30 personal learning PDF outside the repository.
