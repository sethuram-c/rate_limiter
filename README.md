# Lock-Free Per-IP Rate Limiter

A from-scratch systems-programming project that will evolve into a high-performance,
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

## Phase 1 status

Phase 1 establishes a reproducible repository and strict compiler baseline. The
current executable is intentionally only a scaffold; rate-limiting logic begins in
the next phase.

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

Expected test result:

```text
smoke test passed: lockfree-rate-limiter: project scaffold ready
```

## Repository layout

```text
.
├── bench/       # performance benchmarks added in later phases
├── include/     # public/internal headers added in later phases
├── src/         # implementation
├── tests/       # correctness and concurrency tests
├── .gitignore
├── Makefile
├── README.md
└── progress.md
```

Learning PDFs are deliberately kept outside this repository so the Git history
contains only project artifacts.
