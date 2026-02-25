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
