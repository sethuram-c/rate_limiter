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
