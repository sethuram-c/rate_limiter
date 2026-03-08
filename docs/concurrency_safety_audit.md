# Phase 25 - Concurrency safety and sanitizer audit

This is the repository-facing engineering note. A more detailed learning PDF
with worked exercises and commands is distributed separately, **outside Git**.

## Intended guarantee and boundaries

- `lf_bucket_t` owns one atomic 64-bit combined token/time state. A successful
  CAS linearizes the admission decision. It is lock-free only if the target
  reports lock-free 64-bit atomic operations at initialization.
- The IP index contains nondeleting atomic key slots; keys are published with
  atomic CAS, and all bucket objects are fully initialized **before** requests
  may begin. The table has bounded capacity; no concurrent resize or delete.
- Clock callbacks and custom allocator callbacks have their own contracts.
  Callers must supply a thread-safe clock callback, and no allocator callbacks
  occur on the request hot path.
- `rate_service_t` outcome counters use independent relaxed atomic increments.
  Totals become directly comparable only **after** the workers finish.
- **Destroy is quiescent-only**: stop ingress, join every request thread, then
  destroy the service. Calling `destroy()` concurrently with requests is
  outside the API contract and would risk use-after-free.
- Neither the rate-service wrapper nor a wall-clock callback is proven
  wait-free; `lock-free` refers to the internal CAS data structures, not all
  work in an application or a universal latency bound.

## Concurrent audit and oracle

`tests/test_concurrency_audit.c` creates 8 pthread workers synchronized by a
barrier. Each issues 20,000 deterministic requests (160,000 total) against 16
IP addresses using immutable injected time (no refills). One in five calls is
invalid (zero tokens); valid calls all request one token.

Exactly 16 * 31 = **496 admissions** are possible. Expected invalid count is
8 * (20,000 / 5) = **32,000**. Valid denials must therefore equal
160,000 - 32,000 - 496 = **127,504**. Every bucket ends with zero tokens.
Worker-local counters and service atomics must agree after all joins. The test
also checks table count and independent bucket state and repeats initialization
and shutdown in a separate capacity-exhaustion scenario.

This test is evidence under a meaningful schedule, **not a universal proof** of
linearizability or absence of races in every execution.

## Commands (Linux, Fedora / GCC / Clang)

```sh
make clean && make CC=gcc -j4 test
make clean && make CC=clang -j4 test
make audit-test audit-test-padded audit-runner-test
make benchmark-cli-test
make audit-asan
make audit-tsan
```

`make audit-asan` compiles the focused audit with `-fsanitize=address,undefined`
and stack traces (`-fno-omit-frame-pointer`). ASan detects many memory errors;
UBSan finds selected undefined behavior. Neither is a race detector. Do not
expect performance figures from sanitizer-instrumented builds to describe the
release benchmark's throughput.

`make audit-tsan` compiles with `-fsanitize=thread`, **separate from ASan**,
and executes through `tests/run_tsan.sh`:

- exit `0`: instrumented test ran, no race reported (**limited PASS**)
- exit `77`: sanitizer runtime unavailable (e.g. unsupported memory mapping),
  **SKIP, never a pass**
- exit `1`: report indicates a race, other runtime issue, or test failure

`make audit-runner-test` verifies these distinct outcomes using mock binaries.
If TSAN is unsupported, try the run on a native Linux environment with working
runtime; do not silence or suppress its diagnostics merely to get green output.
When TSAN runs, any conflicting non-atomic accesses are worth investigating,
though sanitizer reports need evaluation for instrumentation/runtime effects.

### Restored benchmark source tracking

`bench/benchmark_rate_service.c` and `tests/test_benchmark.sh` existed in an
older source ZIP but were not added by that phase's `git add`. Phase 25 stages
both. `make benchmark-cli-test` now compiles and exercises the real benchmark
CLI against hot, uniform and skewed workload selections; it validates CSV
request accounting and invalid-argument rejection. No benchmark CSV or Python
cache files are tracked.

### Review checklist

1. Find each non-atomic field accessed by request threads. Confirm it is
   initialized before publication and immutable until all threads join.
2. For each atomic variable, articulate why chosen ordering is sufficient.
3. Verify CAS loops recompute the transition after a failed attempt.
4. Confirm all request inputs validate before IP-slot allocation.
5. Confirm storage cannot be destroyed or moved while any worker still uses it.
6. Verify benchmark and tests are built with identical structure definitions
   when `RL_CACHELINE_PADDED` is enabled.
7. Run compact, padded, sanitizer and CLI checks; inspect return codes.
8. Never claim a negative sanitizer result formally proves freedom from races.

## Next steps

Phase 26 can add reproducible sanitizer/CI jobs on multiple Linux toolchains,
with TSAN reported distinctly as verified, skipped or failing.
