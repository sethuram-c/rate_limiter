# Engineering showcase | Lock-Free Per-IP Rate Limiter

**Snapshot:** Version 0.1.0 (Unreleased). **Code:** C11 + POSIX, Linux/WSL. **Artifact:** in-process static library and examples.

## Project problem and scope

A rate limiter must reject requests that exceed a configured per-client quota without overspending a shared token pool under concurrent load. The design uses a fixed-capacity mapping from each IPv4 address to a preinitialized token bucket. Refill time and available tokens are packed into one atomic state to avoid torn logical updates.

This is **not** a reverse proxy, socket server, middleware for a particular web framework, kernel module, or a production network security product. The gateway returns illustrative status numbers only.

## Architecture in one minute

- Optional immutable CIDR rules: BYPASS or BLOCK before rate-limit evaluation.
- Strict IPv4 gateway parses bounded buffers, separates ingress errors from service errors.
- Rate service samples a monotonic/injected clock and records atomic outcome counters.
- Power-of-two open-addressed atomic key table maps an IPv4 value to a **preinitialized** bucket; no request-path allocation.
- A 64-bit atomic packs 48-bit time and 16-bit token balance. A transition is recomputed after each failed CAS.
- Mutex implementation uses the same key index and transition planner to isolate bucket synchronization for A/B evaluation.

![Four-layer overview](diagrams/components.svg)

The **key-publication CAS** and **token-decision CAS** have separate linearization points. The underlying single bucket can offer lock-free progress where the platform supports it, but the entire service (including callback timing and lifecycle) is not promised lock-free.

## Why these decisions were made

| Decision | Advantage | Cost / limitation |
| --- | --- | --- |
| One packed 64-bit atomic | Consistent token/refill state | 48-bit time and 16-bit token bounds |
| Preallocated insert-only table | Avoids hot-path allocation and reclamation | Fixed capacity; full-table errors, no eviction |
| Injected monotonic time | Deterministic tests and error injection | Caller must supply safe clock callback |
| Immutable policy | Readers need no policy mutex | No concurrent rule updates |
| Quiescent destruction | Simple lifetime management | Caller must stop/join workers first |
| Optional 64-byte padded buckets | Can reduce false sharing for some workloads | Memory overhead; performance varies |

See [ADRs](architecture_decisions.md) for full context and alternatives.

## Evidence you can demonstrate

1. **Correctness:** `make lockfree-test`, `make contention-test`, `make impossible-request-test`, and `make api-contract-test` exercise atomic consumption, short histories, resource exhaustion, and bounded parsing. Tests reject impossible histories but do not formally prove the entire algorithm.
2. **Concurrency:** `make audit-test` checks aggregate request outcomes under pthread load. Address/UB sanitizers and manual TSan checks offer additional evidence; a clean sanitizer result is not a guarantee.
3. **Packaging:** `make package-test` stages installation and compiles an independent consumer against installed headers/static archive.
4. **Comparison:** `make evidence-smoke` records paired mutex/CAS runs and machine metadata in ignored `build/` files. Long measurements should be repeated on the same machine with warmups and controlled workload conditions.
5. **Documentation:** `make architecture-docs-test` and `make portfolio-docs-test` validate diagrams, links and portfolio claim consistency.

**No universal speedup claims. Numbers to avoid inventing:** universal speedup, production HTTP RPS, whole-project branch/line coverage, and formally proven lock-freedom of the entire public API. The sample `gcov` probe instrumented only the sequential token bucket, not all code.

## Five-minute interviewer narrative

**0:00–0:45:** Explain the goal and why a per-IP bucket needs isolated state.

**0:45–1:45:** Open [component diagram](diagrams/components.svg); explain the gateway, time source, IP index, preinitialized buckets, and CAS transitions.

**1:45–2:45:** Show `src/lockfree_bucket.c` and `src/bucket_state.c`. Point out a successful CAS and why failed CAS recomputes.

**2:45–3:45:** Show tests, failure paths, worker lifecycle and the mutex comparison. Discuss rejected oversized requests without table-slot admission.

**3:45–5:00:** Run `make docs-test` and/or `make evidence-smoke`; discuss workload-specific results and documented limitations.

Follow the reproducible [demo script](project_demo.md) for command explanations.

## Resume bullet templates (verify on your own hardware)

- **Built** a C11 per-IP token-bucket rate limiter using an atomic IPv4 index and packed-state CAS, with deterministic refill tests and multithreaded correctness checks.
- **Validated** request decisions and failure handling using pthread stress tests, short-history linearizability checks, allocation-failure injection, sanitizer runs, and GCC/Clang CI.
- **Benchmarked** a CAS bucket against a mutex reference using matched workloads, paired throughput/latency measurements and reproducible environment metadata; documented workload-dependent trade-offs.

Avoid inserting a numeric speedup into the bullets until you have repeated results, recorded environment settings, and inspected admission accounting.
