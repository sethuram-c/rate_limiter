# Lock-Free Per-IP Rate Limiter (C11)

A fixed-capacity, in-process IPv4 token-bucket rate limiter built with C11 atomics and POSIX threads. It combines **per-IP quotas**, **a CAS-updated packed bucket state**, and a **concurrent open-addressed IP index**, with a mutex reference implementation for matched correctness and benchmark comparisons.

**Status:** `0.1.0` · **Unreleased** · Linux/WSL · static library (`libratelimiter.a`). This is a portfolio/systems project, **not an HTTP server** or a production-ready drop-in network gateway.

## Try it on Fedora

```bash
sudo dnf install gcc clang make python3 git
make clean
make CC=gcc library
make package-test        # install into a temporary staging directory, then link an external consumer
make docs-test           # public-API documentation and installed-library walkthrough
make test                # complete C/Python/shell regression suite
```

For a short demonstration:

```bash
make request-gateway-demo
make request-policy-demo
```

To verify both compilers and the most important contracts separately:

```bash
make quality-quick
make static-check        # GCC and Clang static analysis
make audit-asan          # targeted ASan and UBSan concurrency audit
make ci-config-test      # GitHub Actions workflow checks
make portfolio-docs-test # verify links, commands, version and portfolio claims
```

`make test` and `make quality-release` can be longer-running because of concurrency and benchmark smoke tests. A sanitizer run or bounded concurrency test is **evidence**, not a mathematical proof for all schedules.

## How it works

![Architecture: ingress → service → per-IP index → atomic buckets](docs/diagrams/components.svg)

1. The optional **CIDR policy** bypasses or blocks configured IPv4 ranges without invoking the limiter.
2. The **gateway** validates an IPv4 address (including length-bounded buffers) and maps decisions to illustrative HTTP-style codes.
3. The **rate service** samples injected or Linux `CLOCK_MONOTONIC` time and records outcome counters.
4. The concurrent, **fixed-capacity key index** assigns each previously unseen IPv4 address a preinitialized bucket slot.
5. An `_Atomic uint64_t` stores **48-bit refill time** and **16-bit token balance**. Each request recomputes a transition and uses CAS to commit it.

The key-publication CAS and token-consumption CAS are **different operations**. This is a lock-free bucket update where `atomic_is_lock_free()` succeeds, **not** a promise that every library API or calling thread is lock-free or wait-free. Custom clocks, application logic, and shutdown may block.

Read the [architecture guide](docs/system_architecture.md), [four editable diagrams](docs/diagrams/), and [design decisions](docs/architecture_decisions.md).

## Core engineering features

| Area | Implementation |
| --- | --- |
| Per-IP quota | Configurable burst capacity and integer refill interval |
| Concurrency | CAS token update, atomic IP key index, pthread stress tests |
| Deterministic testing | Injected clock, reference-model/property tests, bounded history checker |
| Resource safety | Preallocation, configuration/memory-budget checks, allocation-failure injection |
| Operational behavior | Decision-aligned remaining tokens, retry hints, five atomic outcome counters |
| Request adapter | Bounded IPv4 parsing, immutable CIDR bypass/block rules, HTTP-style status mapping |
| Evaluation | Mutex reference, throughput and p50/p95/p99 latency benchmarks, raw paired evidence |
| Engineering quality | GCC/Clang, static analysis, sanitizers, GitHub Actions, static-library packaging |

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

The example checks version strings, initializes quota configuration, tests bounded IPv4 ingress and CIDR policy behavior, and destroys the service after requests stop. The automated equivalent is `make docs-test`. For interfaces and lifecycle contracts, see the [public API reference](docs/public_api_reference.md) and [integration guide](docs/developer_integration_guide.md).

## Correctness and performance evidence

Tests include deterministic token-state transitions, concurrent admission accounting, CAS contention counters, negative controls for bounded linearizability histories, resource-failure injection, and sanitized runs. **Bounded history checking is not a universal formal proof.**

```bash
make contention-test
make impossible-request-test
make evidence-test
make evidence-smoke
cat build/evidence_report.md
```

The performance tooling compares **mutex versus CAS bucket synchronization** under matched shared/sharded workloads, with median paired ratios and variability. It does **not** benchmark an HTTP endpoint, and observed ratios are hardware/workload-specific; no universal speedup is claimed. Generated CSVs and machine metadata belong in ignored `build/`. See the [benchmark methodology](docs/performance_evidence.md) and [reproducible experiment guide](docs/benchmark_methodology.md).

## Important limitations

- IPv4 only; no IPv6, network listener, HTTP serialization or trusted-proxy authentication.
- Fixed-size, insert-only IP table: **no eviction, concurrent deletion or resizing**; a full table rejects unseen IPs.
- All workers must finish and be joined **before** the service is destroyed; concurrent destroy is invalid.
- Immutable policies during worker processing; injected clocks must honor the callback and time contracts.
- Five independent atomic counters are **not a transactional snapshot** while requests are active.
- Version `0.1.0` is unreleased and does **not** promise a stable binary ABI.

## Project resources

- [Production assumptions and unresolved deployment risks](docs/production_readiness.md) · [Machine-checked risk register](docs/production_risks.json)

- [Engineering highlights and interview talking points](docs/portfolio_overview.md)
- [Five-minute demo script](docs/project_demo.md)
- [System architecture](docs/system_architecture.md) · [Architectural decisions](docs/architecture_decisions.md)
- [Public API reference](docs/public_api_reference.md) · [Developer integration](docs/developer_integration_guide.md)
- [Performance evidence](docs/performance_evidence.md) · [Release quality gates](docs/release_quality_gates.md)
- [Change log](CHANGELOG.md) · [Development progress](progress.md)

The **phase-by-phase learning PDFs are personal study materials and are intentionally kept outside this Git repository**.
