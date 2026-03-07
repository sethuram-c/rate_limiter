# Development Progress

This file records what each implementation phase changes and the acceptance criteria
that were met. Detailed learning notes live outside the repository.

## Phase 1 - Repository and build baseline

Status: complete

### Added

- initial project directory structure
- strict C11 Makefile
- scaffold executable
- Bash smoke test
- `.gitignore`
- initial README and project goals

### Acceptance criteria

- `make clean && make` succeeds with warnings treated as errors
- `make run` executes the generated binary
- `make test` passes
- generated artifacts stay under `build/` and are ignored by Git
- learning material is not stored in the repository

## Phase 2 - Sequential token bucket

Status: complete

### Added

- `token_bucket_t` sequential state model
- public token-bucket header and implementation
- deterministic time injection through `now_ns`
- capacity-limited whole-token refill logic
- focused C unit tests for boundaries and invalid input
- deterministic command-line demo
- updated smoke test and Makefile test targets

### Acceptance criteria

- bucket starts at configured capacity
- requests consume exactly one token
- empty bucket rejects requests
- no refill occurs before a full interval elapses
- elapsed partial intervals are preserved across calls
- long idle periods refill only to capacity
- stale timestamps cannot mint tokens
- invalid configuration is rejected
- strict build succeeds under `-Wall -Wextra -Wpedantic -Werror`
- `make test` passes

### Next phase

Add a monotonic-clock abstraction and separate policy time from real Linux time so
production calls can use `clock_gettime(CLOCK_MONOTONIC, ...)` while unit tests remain
deterministic.

## Phase 3 - Linux monotonic clock adapter

Status: complete

### Added

- `monotonic_clock_now_ns()` sampling `CLOCK_MONOTONIC`
- `monotonic_clock_timespec_to_ns()` for checked, deterministic conversion
- valid/invalid/overflow conversion tests and nondecreasing-clock test
- `--live` demo mode while preserving the Phase 2 deterministic default
- strict Makefile clock targets, POSIX feature macro, and shell smoke coverage
- updated README and progression notes

### Acceptance criteria

- both existing token-bucket behavior tests and new clock tests pass
- adapter uses `CLOCK_MONOTONIC` rather than `CLOCK_REALTIME`
- no sleeping or wall-clock dependencies in algorithm unit tests
- nanoseconds stay in the range `[0, 1,000,000,000)` for each timespec component
- negative seconds/nanoseconds, null arguments, and arithmetic overflow are rejected
- failures do not mutate the caller's output when non-null
- real sampled values are nondecreasing in the smoke environment
- deterministic CLI mode remains byte-for-byte compatible with Phase 2
- `make clean && make && make test && make run-live` works with strict compiler warnings

### Next phase

Strengthen the sequential token-bucket API and its edge-case test matrix before
introducing concurrency; then start focused C11 atomic programming exercises.

## Phase 4 - Boundary invariants and correctness matrix

Status: complete

### Added

- documented sequential invariants and caller obligations
- `token_bucket_try_consume_n()` for all-or-nothing multi-token requests
- retained single-token API as a wrapper around the shared consumption path
- explicit overflow proof for whole-interval anchor advancement
- edge tests for invalid input, refill failure, stale time, `UINT64_MAX`
  timestamps, `UINT64_MAX` capacity, and large refill intervals
- separate deterministic reference-model test covering 48,000 mixed operations
- `make properties-test` plus integration into `make test`

### Acceptance criteria

- all batch requests are atomic in the single-threaded *logical* sense:
  either the entire requested amount is taken or none of it is
- a zero-amount batch leaves the bucket untouched
- a rejected insufficient-funds batch may still accrue elapsed-time refill
- token balance stays in `[0, capacity]` at every operation
- refill anchor never moves backward for stale caller timestamps
- arithmetic never overflows for valid initialized state and `uint64_t` time
- reference implementation agrees with production across 48,000 operations
- GCC, Clang, and sanitizer checks pass on Linux
- deterministic and live CLI behavior remains unchanged

### Next phase

Introduce self-contained C11 atomic operations experiments. The sequential
bucket intentionally remains not thread-safe until the concurrency design
and state representation are established.

## Phase 5 - C11 atomics, compare-and-swap, and publication

Status: complete

### Added

- `include/atomic_lab.h` and `src/atomic_lab.c` as an isolated educational module
- bounded CAS counter using `atomic_uint_fast64_t` and relaxed memory ordering
- one-shot mailbox demonstrating release/acquire synchronization
- contention test with 6 pthread workers, 120,000 attempts and a limit of 50,000
- one-publisher / six-reader payload publication test
- `atomic_is_lock_free` diagnostic and an executable teaching demo
- `make atomic-test` and `make atomic-demo`, integrated into `make test`
- updated README explaining what is and is not thread-safe

### Acceptance criteria

- all earlier deterministic bucket, clock, model, and CLI tests still pass
- CAS increments cannot overshoot the configured limit, even with contention
- the CAS loop tolerates weak compare-exchange failure and uses valid failure ordering
- a successful CAS is the counter increment's linearization point
- acquire readers observe the single writer's initialized payload
- the counter's lock-free status is queried rather than assumed
- GCC and Clang clean builds pass strict warnings
- the learning PDF and build files remain outside Git

### Next phase

Model exactly which token-bucket fields must change together before building a
concurrent representation; distinguish linearizable accounting from timestamp
publication and identify where a CAS state transition would linearize.

## Phase 6 - Packed concurrent-bucket state design

Status: complete

### Added

- `include/bucket_state.h` and `src/bucket_state.c`: one-word packed state
  encoding (48-bit monotonic milliseconds / 16-bit token balance)
- pure transition planner that returns the proposed next snapshot and verdict
- explicit policy, range, state and zero-request validation
- safe whole-period refill, preservation of partial intervals, stale-time clamp
- 24,000 deterministic transitions compared with the Phase 4 reference
- tests for maximum packed timestamp, maximum token capacity and saturation
- standalone packed-state demonstration and design explanation under docs/
- `make state-test` and `make state-demo` integrated into existing Makefile

### Acceptance criteria

- packing and unpacking are exact at the 64-bit representable limits
- tokens and timestamp are planned together, never as independent updates
- malformed input leaves output parameters unchanged
- ordinary rate limit denials are distinguished from invalid inputs
- pure transitions agree with the sequential reference for 24,000 operations
- all previous project tests and demos still pass with strict compiler flags
- guide PDF stays outside the source repository

### Next phase

Implement an actual atomic bucket with a CAS retry loop that commits the
complete packed state; test it under pthread contention and check lock-free
capability on the target system. Do not claim thread safety before then.

## Phase 7 - Lock-free token bucket using atomic CAS

Status: complete

### Added

- `include/lockfree_bucket.h` and `src/lockfree_bucket.c`: fixed-policy
  lock-free token bucket that atomically commits a Phase 6 state transition
- runtime 64-bit atomic lock-free check before accepting initialization
- weak CAS retry loop with relaxed ordering and full transition recomputation
- separate return values for invalid operation versus ordinary quota denial
- coherent atomic snapshot utility and explanatory architecture document
- eight-thread contention tests (800,000 total attempts), zero-overspend
  assertions, batch atomicity, refill, stale time, and packed maximum limits
- reproducible demonstration executable and Makefile integration

### Acceptance criteria

- one successful CAS commits the entire token/refill state
- a failed CAS retries with the refreshed actual state, not a stale plan
- no token overdraft with eight simultaneous pthread workers
- simultaneous three-token requests can only claim a whole three tokens
- denied calls and snapshots have coherent atomic observations
- stale timestamps never reverse the stored refill anchor
- initialization rejects invalid policy and non-lock-free atomic targets
- all earlier Phase 1-6 functionality still builds and passes tests
- separate learning guide never enters the repository

### Next phase

Investigate contention fairness, linearizability test techniques and
thread-sanitizer coverage before integrating per-IP hash table storage.

## Phase 8 - Contention analysis and bounded linearizability

Status: complete

### Added

- Optional per-call CAS-attempt instrumentation without global counter sharing
- Eight-worker, ten-round reproducible stress schedule (960,000 calls)
- Admission counts, denial counts, CAS retries and max-attempt reports
- Bounded exhaustive history checker enforcing happens-before constraints
- 100 real concurrent eight-operation traces, with mixed timestamps and amounts
- Negative controls rejecting impossible over-admissions and final states
- Documentation and `make contention-test`, integrated into `make test`

### Acceptance criteria

- No tokens are overspent across 960,000 attempts at a fixed timestamp
- Counters satisfy total attempts = admitted + denied
- Successful operations report at least one CAS attempt
- Negative fabricated histories are rejected, real test histories accepted
- All existing tests remain green under GCC and Clang strict builds
- PDFs and build artifacts remain outside the Git repository

### Next phase

Design the fixed-capacity per-IP hash table; define safe initialization and
key-publishing semantics before implementing concurrent slot claims.

## Phase 9 - Fixed-capacity per-IP hash-table design

Status: complete (sequential reference model, not concurrent insertion)

### Added

- `include/ip_table_plan.h`, `src/ip_table_plan.c`: deterministic IPv4 hash,
  nonzero power-of-two capacity checks, bounded linear probing
- Explicit separate occupancy flag, so 0 and UINT32_MAX remain valid keys
- `IP_PROBE_FOUND`, `IP_PROBE_EMPTY`, `IP_PROBE_FULL` result semantics
- Idempotent sequential insertion with independent insert status
- Forced collision/wraparound tests, full-table/duplicate-key tests, invalid
  argument tests, and 60,000 deterministic reference-model operations
- Standalone demonstration, Makefile targets, and hash-table design document

### Acceptance criteria

- Same IP always hashes to the same start slot for a given table capacity
- Every probe scans at most `capacity` slots and wraps correctly
- Duplicate insertion reuses the existing slot without growing occupancy
- All 32-bit IPv4 keys are representable, including 0 and UINT32_MAX
- Full-table condition is explicit and does not overwrite existing entries
- Out parameters remain unchanged on invalid calls
- GCC and Clang strict builds and previous-phase tests pass
- Phase learning guide and compiled artifacts stay outside Git

### Next phase

Implement atomic slot publication and deduplicated concurrent lookup/insertion;
make memory management and whether insertion itself qualifies as lock-free
explicit. Never mutate the sequential reference-model slots concurrently.

## Phase 10 - Concurrent IPv4 key index

Status: complete (key table only; per-IP bucket integration is Phase 11)

### Added

- `ip_atomic_table`: fixed-capacity, insert-only open-addressed IPv4 index
- Atomic tagged 64-bit slots supporting every IPv4 key, including 0 and UINT32_MAX
- Strong-CAS publication, duplicate-key deduplication, bounded probes
- Conservative sequential-consistency ordering across probe chains
- Preallocated slots and `atomic_is_lock_free` capability checks
- Concurrent pthread tests: 204,800 operations on 128 keys, 24-way table fill
- New demo, Makefile targets and correctness/lifetime documentation

### Acceptance criteria

- Exactly 128 INSERTED results across 204,800 shared-key operations
- Exactly 16 INSERTED and 8 FULL in 24-way saturation test
- No duplicate slots for an IPv4 key; stable lookup after thread join
- Table never permits mutation, deletion, or reclamation during use
- All old phase tests pass under strict GCC and Clang builds
- Personal learning PDF and generated build artifacts remain outside Git

### Next phase

Associate a fully initialized lock-free token bucket with each published IP
without ever exposing partially initialized state. Keep the table lifecycle
rules and avoid false claims about lock-free allocation or general-purpose
resizing.

## Phase 11 - Per-IP lock-free bucket integration

Status: complete (library-level per-IP rate limiting; not a network service)

### Added

- `ip_rate_limiter_t`, a composition of the atomic IPv4 key index and an
  independently initialized `lf_bucket_t` for each physical slot
- Pre-initialization of all bucket payloads **before** threads may claim keys,
  removing the race between key publication and bucket construction
- Allocation-free concurrent decision API with explicit ALLOWED, DENIED,
  TABLE_FULL and INVALID results
- Snapshot helper, deterministic demonstration, make targets and lifecycle doc
- 12-thread tests: 90,000 requests for one IP, 76,800 over 64 IPs

### Acceptance criteria

- 173 admissions for one-IP overload; 2,368 for 64-IP overload
- Same-IP contention never overdraws bucket; each independent IP has own quota
- Invalid calls do not claim an IP slot; full table reports distinct status
- All IPv4 keys, including 0 and UINT32_MAX, remain supported
- No uninitialized bucket is published; destruction is quiescent-only
- Previous phases build and tests pass under strict GCC and Clang
- PDF guide and compiled outputs stay outside the repository

### Next phase

Introduce a cleaner higher-level rate-limiter API and controlled clock adapter,
including clear error handling for resource exhaustion and testable per-IP
request flows; retain the lifetime and no-allocation request-path constraints.

## Phase 12 - Clock-aware in-process request API

Status: complete (real clock integration; **not** a network-facing server)

### Added

- `rate_service_t` with injectable thread-safe millisecond clock callback
- `rate_service_monotonic_ms()` converting POSIX monotonic ns to integer ms
- `rate_service_init_monotonic()` and explicit ALLOW/DENY/FULL/INVALID/CLOCK_ERROR
- Validation before clock sampling and clock sampling before claiming IP keys
- Failure-safe initialization and quiescent destruction/reinitialization
- Deterministic and live-clock CLI demonstrations
- 12-thread test with 60,000 requests and exactly 74 admissions across 2 IPs
- Linux/API documentation, focused and full Makefile verification targets

### Acceptance criteria

- Exact refill and IP-isolation behavior with controlled injected time
- Failed clock read or invalid amount never claims an index slot
- Table exhaustion is distinct from insufficient tokens
- Monotonic adapter returns integer milliseconds, bounded by before/after reads
- Concurrent request decisions never exceed 37 admissions per IP in test
- GCC/Clang strict builds and every prior test remain green
- PDF learning guide and compiled artifacts stay outside source repository

### Next phase

Harden the public API and resource handling; consider policy validation,
capacity planning, and robust service configuration. Do not add a network
server prematurely or claim all callback implementations are lock-free.

## Phase 13 - Configuration validation and resource budgets

Status: complete (opt-in checked initializer; library remains in-process)

### Added

- `rate_config_t` and an allocation-free planner with typed status diagnostics
- Exact target-ABI payload estimates for atomic key array and bucket array
- Multiplication/addition overflow guards before checking memory budgets
- Explicit power-of-two sizing, packed-field token and refill limits
- `rate_service_init_with_config()` prevalidating BEFORE clock/alloc side effects
- Improved validation order in the backwards-compatible base initializer
- Deterministic targeted tests, demonstrator, Makefile targets and API docs
- Tested 2,200 valid configuration plans, plus invalid and failure-path cases

### Acceptance criteria

- Bad policy/slots/budget leaves service empty; no clock callback is invoked
- Exact budget accepted, one byte below budget rejected
- Arithmetic never wraps during array size planning
- No partial mutation of output plan on any failed validation
- Existing rate-service and previous-phase regression tests remain green
- GCC/Clang strict full suite, UBSan full suite and Phase 13 ASan tests pass
- Personal PDF, binary artifacts and `.git` excluded from source ZIP

### Next phase

Improve the rate limiter's external diagnostics and request policy behavior
without weakening the current concurrency and error-handling contracts.


## Phase 14 - Request diagnostics and policy response metadata

Status: complete

### Added

- same-CAS committed-state reporting without a second atomic read
- optional per-IP and service request diagnostic APIs
- token balance, refill anchor, and policy fields tied to the decision
- overflow-checked earliest retry estimate for satisfiable denied batches
- explicit handling for impossible requests, clock failures and table full
- deterministic boundary tests plus 32,000 threaded requests
- independent demo, docs and Makefile targets

### Acceptance criteria

- decision and reported remaining balance originate from the same CAS
- existing public request APIs and earlier tests remain compatible
- no extra allocation or locking is required for request diagnostics
- errors and table-full returns never pretend to have bucket state
- retry time is only a hint and respects 48-bit timestamp bounds
- GCC/Clang strict builds and test suite succeed
- learning PDF stays outside the Git repository

### Next phase

Introduce policy-level testing and practical caller integration examples,
including operational error classification and status reporting.


## Phase 15 - Operational status and policy classification

Status: complete

### Added

- C11 lock-free-checked relaxed-atomic counters for all five service outcomes
- one increment for every completed request on a valid initialized service
- coverage of both legacy and diagnostic request APIs with no double counting
- non-transactional snapshot API and pure outcome classification helpers
- deterministic error/limit tests and mixed-API concurrent workload
- operational documentation, CLI demo and independent `make rate-status-test`

### Acceptance criteria

- policy classifications preserve all five distinct service outcomes
- invalid, denied, clock error, table full, and allowed outcomes counted once
- detailed NULL-output call makes no request and does not change stats
- counters are only read on initialized service; malformed calls remain safe
- exactly 148 allowed in 160,000 requests over 4 IPs at fixed time
- GCC/Clang strict suites and sanitizer targeted tests pass
- source ZIP contains no Git database, PDFs, or compiled binaries

### Next phase

Add caller-side adapters or richer end-to-end simulations and evaluate
operational tradeoffs without confusing counters with instantaneous snapshots.

## Phase 16 - In-process request gateway and reproducible traffic scenarios

Status: complete (application adapter and deterministic simulation, **not** a web server)

### Added

- Strict IPv4 dotted-decimal ingress parsing with `inet_pton` and `ntohl`
- HTTP-style response classification with 200 / 400 / 429 / 503 codes
- Decision-aligned remaining balance and optional millisecond retry hint
- Explicit `forwarded` flag differentiating local rejection from service calls
- Eleven-event deterministic timeline covering refill, independent IPs,
  malformed text, clock failure, table exhaustion, and invalid token amounts
- Concurrent 40,000-request adapter test across 4 IPs, exactly 148 admissions
- Documentation, demo and dedicated Makefile targets

### Acceptance criteria

- Malformed IPv4 text does not access the service, clock, tokens or counters
- Valid zero-cost requests remain visible in service invalid counters
- Retry is an earliest-eligibility hint, not guaranteed acceptance
- Final service counts match 10 forwarded requests out of 11 events
- Concurrent adapter calls never overspend four independent 37-token buckets
- Full previous tests remain green under GCC/Clang and targeted sanitizers
- Learning PDF, Git history and build artifacts stay out of source ZIP

### Next phase

Explore configuration/policy integration at application boundaries, additional
structured scenarios, or profiling. Keep the distinction between a library
adapter and a deployed network server explicit.

## Phase 17 - Immutable CIDR policy configuration and advanced ingress scenarios

Status: complete (static, in-process application policy; NOT a deployed HTTP server)

### Added

- Strict IPv4 CIDR parsing, network normalization and fixed 32-rule capacity
- Longest-prefix precedence with BLOCK winning equal-prefix ties
- Static BYPASS (HTTP-style 200) and BLOCK (403), both without service calls
- Delegation to the original gateway for unmatched, malformed or zero-cost inputs
- Explicit route metadata that distinguishes policy outcomes from quota denials
- Deterministic multi-route tests, full-capacity tests and 40,000 pthread requests
- Demo, docs and Makefile targets without modifying the lock-free bucket core

### Acceptance criteria

- Rule additions never partially mutate the table on invalid/full input
- Empty policy behaves like Phase 16 request gateway
- Exemptions and blocks never claim IP slots or increment service counters
- Malformed IPs remain ingress errors; valid zero-token calls are service invalids
- Concurrent test: 10,000 bypass + 10,000 block + 10,000 malformed,
  plus 41 admits and 9,959 throttles among 10,000 forwarded requests
- Compiler and sanitizer verification, full previous regression suite
- Phase 17 learning PDF stored outside the Git repository

### Next phase

Improve distribution-level integration and reproducible workload measurement;
keep the in-process library scope explicit.

## Phase 18 — Reproducible performance workloads (2027-01-01)

- Added fixed-clock shared/sharded throughput driver with deterministic correctness assertions.
- Added benchmark smoke tests, build targets, and benchmark methodology.
- Measured throughput excludes first-time slot creation, includes pthread startup/join.
- Learning PDF remains outside repository.

## Phase 19 - Benchmark repeatability and thread-scaling analysis (2027-01-02)

- Added `bench/scaling_report.py`, a Python-standard-library command-line
  runner for repeated C workload measurements, run order rotation, robust
  statistics, and stable raw/summary CSV exports.
- Added `tests/test_scaling_report.py` with parse/accounting rejection tests,
  median/p10/p90/MAD/speedup checks and real tiny subprocess integration.
- Added `make scaling-test` and `make scaling-report` targets; full `make test`
  includes the new unit/integration checks.
- Explained what the measured throughput includes and excludes, hardware and
  sampling caveats, fixed-total-request interpretation, and Linux inspection.
- Acceptance: 40 recorded default runs and 8 warmup processes, zero accounting
  errors, correct per-scenario baseline, GCC/Clang regression tests green.
- Learning PDF is personal and stored outside the repository.
- Next: add equivalent mutex-protected baseline and controlled A/B comparisons.


## Phase 20 - Mutex reference baseline and paired throughput (2027-01-03)

- Introduced `mutex_rate_limiter_t`: one pthread mutex per preinitialized
  token bucket, existing atomic IP lookup/insertion, identical state planner.
- Added deterministic matching of mutex/CAS outcomes and snapshots, invalid
  input and table-full checks, and an 8-thread/4-IP admission test.
- Added a controlled benchmark: fixed time, matched warmups, identical lookup,
  64 slots, 30,000-token capacity, prestarted threads with atomic start gate.
- Added Python stdlib paired-run runner with alternating A/B order, raw CSVs,
  per-mode medians and median of throughput ratios; parser/CLI tests.
- Explained which costs are isolated, why Phase 18 numbers are not comparable,
  and why any observed ratio requires repetition and environment details.
- Acceptance: `make test`, `make mutex-test`, `make mutex-compare-test` pass;
  distinct shared/sharded runs validate exact admission counts.
- Personal PDF stays outside Git; source ZIP contains no binaries or `.git`.
- Updated `.gitignore` for Python bytecode and Phase learning PDFs.
- Next: richer workloads, hardware counters and careful bottleneck diagnosis.

## Phase 21 - Realistic traffic profiles and bottlenecks (2027-01-04)

- Extended Phase 18's deterministic IP generator with reproducible unit/mixed
  token-cost generation (80% one-token, 20% three-token requests).
- Added `bench/realistic_traffic_bench.c` supporting mutex and lock-free
  implementations, hot/uniform/skewed traffic, 1..16 threads and 1..1024 IPs.
- Prepopulated all IP slots before the timer and used an atomic start gate.
- Verified per-IP token conservation under mixed weights after threads join.
- Added repeated paired workload-matrix reporter with raw and summary CSVs,
  alternating implementation order and validated throughput/accounting.
- Added workload distribution tests, reporter tests and integration smoke runs.
- Added `docs/traffic_bottlenecks.md` with profiling methodology and limits.
- Source ZIP contains full project; personal learning PDF stays outside Git.
- Next: use profiling evidence to evaluate a justified, measurable optimization.

## Phase 22 - Cache-line contention A/B experiment (2027-01-05)

- Preserved compact lock-free bucket as default while adding an opt-in
  `RL_CACHELINE_PADDED` build with 64-byte-aligned atomic state.
- Allocated the padded array with C11 `aligned_alloc`, retaining correct
  per-build `sizeof(lf_bucket_t)` resource estimates and free semantics.
- Added identical compact/padded correctness tests: 80,000 attempts per
  variant, 328 successful admissions, no token overspending.
- Added a real-traffic paired-run reporter, validated raw/summary CSVs,
  alternating order and unit tests. Observed mixed results; did not enable
  padding globally or claim a universal optimization.
- Added cacheline test, sample and report Makefile targets.
- Learning PDF remains outside the Git repository.
- Next: investigate per-operation latency and tail distributions.

- Phase 23: request-level latency sample collection, nearest-rank percentiles, and test coverage.

## Phase 24 - Resource lifecycle and deterministic failure injection (2027-01-07)

- Added an optional quiescent-only allocator interface and matching free
  callback, preserving existing default initialization APIs.
- Added deterministic failure injection for both allocations, verifying
  correct rollback, reinitialization after failed init, and double destroy.
- Applied the allocator to both atomic key index and bucket arrays; padded
  builds request the correct 64-byte alignment.
- Maintained the no-allocation hot path and quiescent teardown contract.
- Added `make lifecycle-test` and `make lifecycle-test-padded` to the suite.
- Personal educational PDF lives outside Git.
- Next: deeper sanitizer/TSan review and concurrency lifetime auditing.
