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
