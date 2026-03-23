# Final project handoff - Phase 40

**Snapshot:** C11 lock-free per-IP rate limiter, version **0.1.0 (Unreleased)**.
**Phase date:** 2027-01-23. **Deployment verdict:** **NOT_PRODUCTION_READY**.
This document describes a demonstrable portfolio/library snapshot, not a released product.

## What is being handed off

- A **static C11 library** (`make library`) with an installed-header consumer test (`make package-test`).
- A **fixed-capacity, insert-only IPv4 index** with one preinitialized token bucket per IP.
- A packed 64-bit refill/token state updated with CAS. The key-publication CAS is separate from the token-decision CAS.
- A monotonic/injected clock request API, bounded IPv4 gateway, immutable CIDR bypass/block policy, diagnostics and atomic counters.
- A mutex reference and reproducible throughput/latency tooling that reports workload-dependent results instead of universal speedups.
- Tests for deterministic refill, allocation-failure rollback, admission accounting, bounded history checks, and resource limitations.

## Prerequisites

Linux/Fedora or compatible Linux/WSL, GCC or Clang with C11 atomics, POSIX threads, Python 3, GNU Make. Optional: Graphviz for diagram regeneration. Installed GitHub Actions execution depends on access to a GitHub remote and is not certified by the local checks.

On Fedora (if needed):

```bash
sudo dnf install gcc clang make python3 git graphviz
```

## Fast handoff path (run at repository root)

```bash
make clean
make CC=gcc library
make package-test
make handoff-test
make handoff-check
make rc-check
make rc-demo
```

`make handoff-check` validates handoff references, evidence targets, risk verdict and documentation consistency. `make rc-check` runs separate release-candidate contract/integration tests and writes a **local** JSON report under ignored `build/`. `make rc-demo` runs practical examples and replaces the RC report with a demo-profile report. Neither is a formal proof of correctness or production certification.

## Deeper verification - run separately, record actual exit codes

```bash
make clean && make CC=gcc test
make clean && make CC=clang test
make static-check
make audit-asan
make quality-release
make evidence-smoke
```

The full suite can be slow on constrained environments. A timeout or an interrupted benchmark run is **not** a passing test. Local compiler/sanitizer results are not equivalent to a hosted GitHub Actions pass. Detailed profiling evidence is hardware/workload-dependent and is generated under ignored `build/`.

## Required safety/lifecycle contracts

1. The integration must provide a trustworthy IPv4 client identity: arbitrary or untrusted proxy headers are not authenticated by the library.
2. The finite table has no eviction, concurrent deletion or resizing; new IPs may receive `TABLE_FULL` when saturated.
3. Worker threads must stop and **join before destruction**; the API does not implement concurrent memory reclamation.
4. Callback clocks and immutable CIDR rule ownership remain the caller's responsibility.
5. The per-request bucket CAS is not a promise that every public call is lock-free or wait-free.
6. The gateway exposes HTTP-*style* statuses, not a socket server or HTTP protocol stack.
7. Counters read separately are not a coherent snapshot under active concurrency.

For source-linked blockers and mitigating actions see [production readiness](production_readiness.md), [structured risk register](production_risks.json), and the [public API reference](public_api_reference.md).

## What is deliberately NOT delivered

- No IPv6, network listener, HTTP server, trusted-proxy enforcement, table eviction, or live policy updates.
- No stable ABI promise, published Git tag, hosted GitHub release, deployment certification, or service-level SLA.
- No invented HTTP throughput, universal mutex/CAS speedup, formal end-to-end proof, or whole-project 100% coverage claim.

## What to inspect for an engineering review

| Question | Where to inspect | Repeatable command |
| --- | --- | --- |
| How does the bucket update safely? | `src/lockfree_bucket.c`, `src/bucket_state.c` | `make contention-test` |
| How are new IPs admitted? | `src/ip_atomic_table.c`, `src/ip_rate_limiter.c` | `make ip-rate-test` |
| What happens under memory pressure? | `src/rate_allocator.c`, `tests/test_resource_lifecycle.c` | `make lifecycle-test` |
| Can a stranger use the installed library? | `examples/public_api_walkthrough.c` | `make package-test` |
| Are policy limits honest? | `docs/production_risks.json` | `make risk-audit` |
| How do mutex and CAS compare? | `bench/mutex_compare.c`, `docs/performance_evidence.md` | `make evidence-smoke` |

## Reproducibility and handoff discipline

- Keep personal per-phase learning PDFs **outside** the Git repository. Keep binaries, benchmark CSVs and generated verification JSON under ignored `build/`.
- The committed source snapshot should have a clean Git status after its single Phase 40 commit.
- Before citing an experimental number, repeat the exact workload on the target machine and capture its metadata and raw records.
- Do not move the experimental `0.1.0` label or change `NOT_PRODUCTION_READY` to imply a release.

The final project story and appropriate resume language live in [resume and interview notes](resume_interview_kit.md). The visitor-facing overview remains [the repository README](../README.md). The full chronological development record remains [progress.md](../progress.md).
