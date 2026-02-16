# Lock-Free Per-IP Rate Limiter

A from-scratch systems-programming project evolving into a high-performance,
thread-safe, per-IP token-bucket rate limiter written in C11.

## Project goals

The finished project is intended to demonstrate:

- C11 systems programming and API design
- token-bucket rate limiting
- atomic operations and memory ordering
- lock-free request-path design
- fixed-capacity concurrent hashing
- correctness testing under contention
- benchmarking and performance analysis

## Current status - Phase 2

Phase 2 introduces the first working rate-limiting algorithm: a deterministic,
single-threaded token bucket. It deliberately does **not** use atomics yet. The goal
is to establish and test the sequential state machine before concurrency is added.

The bucket:

- starts full
- consumes one token for each admitted request
- denies requests while empty
- refills one token for every complete configured interval
- never exceeds its configured capacity
- accepts time as an input, making tests deterministic

## Requirements

- Linux or WSL2
- GCC or Clang with C11 support
- GNU Make
- Bash
- Git

On Ubuntu/Debian:

```bash
sudo apt update
sudo apt install -y build-essential git clang gdb valgrind
```

## Build and verify

```bash
make clean
make
make run
make test
```

Expected test output includes:

```text
token bucket tests passed
smoke test passed: deterministic token bucket demo behaves as expected
```

## Repository layout

```text
.
├── bench/                   # performance benchmarks added later
├── include/
│   └── token_bucket.h       # sequential token-bucket API
├── src/
│   ├── main.c               # deterministic demonstration program
│   └── token_bucket.c       # token-bucket implementation
├── tests/
│   ├── smoke.sh             # end-to-end behavior smoke test
│   └── test_token_bucket.c  # focused algorithm tests
├── .gitignore
├── Makefile
├── README.md
└── progress.md
```

Learning PDFs are deliberately kept outside this repository so Git history contains
only project artifacts.

## Design note

The algorithm currently refills in whole-token intervals. That keeps the arithmetic
integer-only and makes boundary behavior explicit. Later phases will add a monotonic
clock adapter, per-IP state, concurrency, atomics, and lock-free data structures.
