# Lock-Free Per-IP Rate Limiter

A from-scratch systems-programming project evolving into a high-performance,
thread-safe, per-IP token-bucket rate limiter written in C11.

## Project goals

The finished project is intended to demonstrate:

- C11 systems programming and API design
- token-bucket rate limiting
- Linux monotonic time and defensive integer conversion
- atomic operations and memory ordering (future phases)
- lock-free request-path design (future phases)
- fixed-capacity concurrent hashing (future phases)
- correctness testing and performance analysis (future phases)

## Current status - Phase 4

Phase 4 strengthens the sequential token bucket with overflow/boundary tests,
an all-or-nothing multi-token consumption API, and a deterministic reference-model
comparison. There are **no atomics or per-IP buckets yet**.

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
```

Expected test output includes:

```text
token bucket tests passed
monotonic clock tests passed
token bucket property tests passed: 48000 deterministic operations
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
├── include/
│   ├── monotonic_clock.h        # Linux/POSIX time adapter API
│   └── token_bucket.h           # deterministic bucket API
├── src/
│   ├── main.c                   # deterministic and live demos
│   ├── monotonic_clock.c        # POSIX clock and safe conversion
│   └── token_bucket.c           # sequential token-bucket policy
├── tests/
│   ├── smoke.sh                 # end-to-end CLI checks
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

## Further verification (Linux)

```bash
make clean && make && make test
make clean && make CC=clang && make CC=clang test
make clean && make CFLAGS='-std=c11 -Wall -Wextra -Wpedantic -Werror -O1 -g -fsanitize=undefined -fno-omit-frame-pointer' LDFLAGS='-fsanitize=undefined' test
make clean && make test
```

The learning PDF for this milestone lives **outside** the Git repository.
