# Phase 27 - Static Analysis and Code Quality Audit

## Scope and reason

The previous phases used compiler warnings, concurrency tests, sanitizers,
and benchmarks. Static analysis complements these runtime checks by reasoning
about execution paths that are difficult to trigger, especially failures that
happen before a benchmark starts.

The new `tools/static_analysis.py` uses tools already familiar to C systems
programmers:

- **GCC** `-fanalyzer -fsyntax-only`: analyzes **all** `.c` translation units
  under `src/`, `bench/`, `examples/`, and `tests/` (55 in this snapshot).
- **Clang** `--analyze -Xanalyzer -analyzer-werror`: analyzes the 16 production
  implementation files under `src/` and the latency benchmark where a real
  bug was discovered, for 17 checks. Full-tree Clang analysis is possible
  with an explicit `--directories` override, but some large stress-test and
  benchmark translation units take prohibitively long to analyze.
- `-std=c11`, POSIX feature flags, warnings-as-errors, include paths, and
  `-pthread` are consistently applied. No code runs, no objects are linked.
- A missing compiler, missing source files, or any diagnostic/error is a
  nonzero exit. CI therefore cannot silently call the check successful.

The default combination runs 72 independent translation-unit checks. This
is **not** a whole-program proof and cannot establish data-race freedom,
lock-free progress, correct memory ordering, or dynamic allocation safety.

## A concrete bug fixed in this phase

In `bench/latency_bench.c`, `all_samples` was allocated before either the
mutex or lock-free limiter initializer ran. If initialization failed, both
branches returned `1` directly, losing the sample buffer. Clang reported:

```
bench/latency_bench.c:146:9: warning: Potential leak of memory pointed to by 'all_samples' [unix.Malloc]
```

Both branches now jump to the already-existing `failed:` cleanup label,
which frees `all_samples` and invokes both idempotent destroy functions.
The successful benchmark path is unchanged. The static analyzer now passes
on the formerly failing file, and `make latency-test` validates the normal
CLI behavior. Failure-path reasoning is vital even for *benchmark* code.

## Fedora workflow

Install the tools:

```bash
sudo dnf install gcc clang make python3
```

From the repository root:

```bash
make static-runner-test   # tests command construction and fail-closed behavior
make static-gcc          # all 55 C translation units
make static-clang        # production code + latency benchmark (17 TUs)
make static-check        # both analyzers
make latency-test        # benchmark functional regression
make ci-config-test      # CI config contract
make test                # all runtime regressions
```

Additional experiments:

```bash
python3 tools/static_analysis.py --tool gcc --directories src
python3 tools/static_analysis.py --tool clang --directories src
# Deliberately broader Clang scope may take a long time:
python3 tools/static_analysis.py --tool clang --directories src bench
```

A Clang analyzer error is promoted to an error with
`-Xanalyzer -analyzer-werror`, not hidden with `|| true`.

## Review checklist

1. **Allocation/cleanup pairing**: enumerate every exit after allocation.
   Verify each free has an owning path and failed initialization is safe.
2. **Input contracts**: check invalid pointers, sizes, overflows, IPv4 strings,
   token counts and failure status propagation.
3. **Concurrency**: confirm all shared bucket changes pass through atomic
   CAS, and immutable configuration is published before workers launch.
4. **Lifetime**: `destroy` is only called after all request threads join.
   Static analyzers do not justify concurrent destruction.
5. **Measurement**: a benchmark must validate admission conservation before
   displaying performance and must free resources after all failure paths.
6. **Repeatability**: clean GCC/Clang builds, ASan/UBSan, and the CI pipeline
   supplement this audit; do not cite analyzer cleanliness as formal proof.

## CI integration

A new required `static-analysis` job in `.github/workflows/ci.yml` executes
`make static-runner-test` and `make static-check` on Ubuntu 24.04. The
previous compiler matrix and sanitizer job are retained. Hosted CI must be
verified after pushing to GitHub; local YAML/contract checks cannot prove
that a GitHub-hosted runner has succeeded.

## Limitation / follow-up

The analyzer inspects translation units individually. Cross-TU ownership,
resource lifetime across all callback combinations, CAS linearizability,
and all possible thread interleavings require other methods (unit tests,
TSan, formal modeling, and engineering review). In Phase 28, we can expand
production-level API contracts and regression/fault-injection coverage.
