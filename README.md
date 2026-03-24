# Per-IP Rate Limiter

A C11 library for applying token-bucket limits to IPv4 clients from multiple threads. It uses a fixed-size IP index and an atomic 64-bit token-bucket state. A mutex implementation is included for testing and performance comparisons.

The library runs inside the calling process. It does not listen on a socket or send HTTP responses.

**Current version:** `0.1.0` (Unreleased). Tested on Linux; the build uses POSIX threads and C11 atomics. This is an experimental implementation, not a drop-in production gateway.

## Try it on Fedora

On Fedora:

```bash
sudo dnf install gcc clang make python3 git
make library
make package-test
make test
```

`make package-test` installs the headers and static archive into a temporary directory and builds a separate consumer. `make test` runs the main regression suite and may take a while.

Useful smaller checks:

```bash
make lockfree-test
make contention-test
make impossible-request-test
make docs-test
make static-check
make audit-asan
make quality-quick
make ci-config-test
make portfolio-docs-test
```

To run the request and CIDR-policy examples:

```bash
make request-gateway-demo
make request-policy-demo
```

Other verification targets are listed in `make help`. In particular, `make rc-check` runs a shorter release-candidate check, `make rc-demo` exercises the installed consumer, and `make quality-release` runs broader release checks. Generated results go under `build/`.

## How it works

![Component diagram](docs/diagrams/components.svg)

A request can pass through an optional immutable CIDR allow/block policy before reaching the limiter. The request adapter validates bounded IPv4 input and returns status values that an application can map to HTTP if needed.

For an ordinary rate-limit decision, the service:

1. Reads `CLOCK_MONOTONIC` (or an injected clock used in tests).
2. Looks up the IPv4 address in a preallocated, open-addressed atomic table.
3. Reads the bucket's packed 64-bit state: a 48-bit refill timestamp and 16-bit token count.
4. Computes the next state and commits it with compare-and-swap (CAS), retrying with a fresh observation if another thread changed the bucket.
5. Returns the decision, remaining tokens or retry hint as appropriate, and updates diagnostic counters.

Publishing a new IP key and updating its bucket use **separate CAS operations**. The bucket update is lock-free on platforms where the required atomic operation is lock-free; this does not make the whole API wait-free. Clock callbacks and lifecycle operations have their own requirements.

See [system architecture](docs/system_architecture.md), [design decisions](docs/architecture_decisions.md), and the [diagrams](docs/diagrams/).

## Core engineering features

The implementation includes fixed-capacity IPv4 indexing, packed CAS bucket state, deterministic clock tests, CIDR policy handling, and a mutex benchmark reference.

## Link it into an application

```bash
make library
make install DESTDIR="$PWD/build/stage" PREFIX=/usr
cc -D_POSIX_C_SOURCE=200809L -std=c11 -Wall -Wextra -Wpedantic -Werror \
  -I"$PWD/build/stage/usr/include/ratelimiter" \
  examples/public_api_walkthrough.c \
  "$PWD/build/stage/usr/lib/libratelimiter.a" \
  -pthread -o build/public_api_walkthrough
./build/public_api_walkthrough
```

The walkthrough covers version checks, quota configuration, bounded IPv4 input, CIDR policy handling, and shutdown after work has stopped. Refer to the [API reference](docs/public_api_reference.md) and [integration guide](docs/developer_integration_guide.md) before embedding the library in an application.

## Correctness and performance evidence

The tests cover token-bucket boundaries, concurrent admission, failed CAS retries, short-history checks, allocation failures, and table exhaustion. Injected clocks make refill tests deterministic. Sanitizer results and bounded histories help find bugs; these checks are not a mathematical proof of correctness for all thread schedules.

The benchmark tools compare the CAS implementation with a mutex-based reference under matched workloads. They report throughput and request latency, including p50/p95/p99, rather than assuming one approach always wins.

```bash
make evidence-test
make evidence-smoke
cat build/evidence_report.md
```

The smoke run checks that the reporting pipeline works; it is not a performance result to quote. For repeatable measurements, see [benchmark methodology](docs/benchmark_methodology.md) and [performance evidence](docs/performance_evidence.md). Keep the machine configuration, raw results, and workload details with any reported numbers.

## Important limitations

- IPv4 only. There is no IPv6 support, network listener, HTTP parser, or client-IP authentication.
- The IP table has fixed capacity and no eviction or resizing. Once full, it rejects previously unseen addresses.
- Workers must stop and join before the service is destroyed. Concurrent destruction is unsupported.
- CIDR rules are immutable while in use. Caller-provided clocks must satisfy the documented thread-safety and time contracts.
- Diagnostic counters are independently atomic, not a consistent multi-counter snapshot while requests are running.
- The `0.1.0` API has no stable binary ABI guarantee.

See the [production-readiness notes](docs/production_readiness.md) and [risk register](docs/production_risks.json) for integration concerns.

## Project resources

- [Public API](docs/public_api_reference.md) and [developer integration](docs/developer_integration_guide.md)
- [Architecture](docs/system_architecture.md) and [design decisions](docs/architecture_decisions.md)
- [Benchmark methodology](docs/benchmark_methodology.md) and [performance results guide](docs/performance_evidence.md)
- [Release checks](docs/release_quality_gates.md) and [release-candidate verification](docs/release_candidate_verification.md)
- [Verification and handoff notes](docs/final_handoff.md) and [project discussion notes](docs/resume_interview_kit.md)
- [Development history](progress.md) and [changelog](CHANGELOG.md)

The additional notes in `docs/` record individual design changes and tests made during development.
