# Phase 39 — Release-candidate verification and final demonstration

**Scope:** verified **unreleased development snapshot** of the C11 static library, version `0.1.0`. This is *not* a production certification, network service, Git tag, GitHub release, or stable ABI pledge. See [production readiness](production_readiness.md) and the [machine-checked risk register](production_risks.json).

## Goals and evidence boundary

The phase creates a reproducible **release-candidate check** and a demonstration that a reviewer can execute on Fedora. Both use the real Makefile and real binary consumers. The verifier emits a machine-readable JSON result with SHA-256 hashes of key source inputs, version metadata, the selected compiler, every completed Make target, pass/fail information and the preserved `NOT_PRODUCTION_READY` verdict. The record is written under ignored `build/` and must not be committed.

This is **evidence of the commands that completed in this run**. A successful quick check does not mean `make test` or CI was executed, does not exercise every schedule, and does not prove network reliability. The program deliberately sets `full_regression_suite_executed=false`, `production_ready=false` and `release_tag_created=false`. The broader `make quality-release` remains a separate, longer activity.

## Fedora prerequisites

```bash
sudo dnf install gcc clang make python3 git
make clean
make rc-test
make rc-check
cat build/rc_verification.json
```

`rc-test` exercises the verifier's own failure semantics without compiling the project. `rc-check` executes seven existing Makefile targets under a per-step timeout. It stops at the first failure and **cannot preserve a stale PASS**. It validates release metadata, the installed-header consumer, public-API walkthrough, oversized-request protection, CAS contention histories, risk documentation and CI configuration. It does not run the whole regression suite or perform a formal proof.

### Short live demonstration

```bash
make rc-demo
cat build/rc_verification.json
```

This commands an installed static-library example, the 11-event gateway trace, CIDR-policy decisions, the never-admissible request capacity guard and the operational risk audit. Use the architecture diagram in [`diagrams/components.svg`](diagrams/components.svg) while explaining the request path.

The demo is an **in-process simulation**: any 200/400/403/429/503 labels are gateway return values, not responses sent over HTTP. The IP address comes from caller-provided input; client identity is not authenticated by this library.

### Cross-compiler and extended verification

```bash
make clean
make CC=clang rc-check
make CC=clang rc-demo
make static-check
make audit-asan
make quality-release    # separate, may be lengthy
```

The tools report results for the compiler used. Do not run an additional check after recording a manifest and assume the previous manifest includes it; repeat the verifier or document that extra command separately.

## Implementation and failure model

`tools/rc_verify.py` is a standard-library-only bounded subprocess runner. `STEPS` stores transparent named Make targets, not fabricated correctness claims. It requires GCC/Clang, Make, `VERSION`, `CHANGELOG.md`, the public header, core implementation files, the risk register and CI workflow. It computes SHA-256 on selected source inputs, and compares the risk-register version/verdict to `VERSION`.

Before running any Make target it removes a previous result. For each command it records the target and exit code (or timeout/error); on the first nonzero outcome it writes `status=failed` and exits with code 1. Invalid options, missing compilers, broken metadata and unwritable output exit with code 2. Successful checks exit with code 0. The report is intentionally generated under `build/` and ignored by Git.

`tests/test_rc_verify.py` checks success, demo scope, source-hash mutation, missing files, poisoned risk verdicts, absence of tools, invalid options, timeouts, stop-on-first-failure, stale-success removal and wrong report locations. The injected subprocess runner allows deterministic negative cases without relying on a real system failure.

## Demonstration script: what to say

1. **Scope (30 s):** C11, per-IP, fixed-capacity, no server, unreleased `0.1.0`.
2. **Architecture (60 s):** ingress parsing and CIDR routing; monotonic clock and atomic index; 48-bit time/16-bit tokens.
3. **Correctness (60 s):** explain why CAS failures retry against the current packed word; show the contention test. Key publication and token consumption are separate CAS actions.
4. **Real integration (60 s):** run `make rc-demo`, review installed consumer output; explain shutdown only after worker joins.
5. **Evidence and limits (90 s):** open `build/rc_verification.json`; point to source hashes and explicit unresolved blockers. Do not call the demo production-certified.

## Troubleshooting

- `RC VERIFY ERROR: required executable missing`: install the compiler or correct `PATH`.
- `RC VERIFY FAILED: ...`: run the named Make target separately to inspect the underlying diagnostic. The manifest should say `failed` and stop at that step.
- `timeout`: increase `--timeout` only after diagnosing a genuine slow workload; do not silently mark a stalled check as passed.
- Package consumer link errors: inspect `include/ratelimiter.h`, `make package-test`, and the static-linking guide.
- `--profile demo` is a demonstration, not the same check set as `--profile check`; its output replaces `build/rc_verification.json`.
- `make quality-release` might take longer than the guided hour. A timed-out command is not a successful full regression pass.

## Interview questions

1. Why are release-candidate checks different from production-readiness certification?
2. How do source hashes improve traceability, and what *don't* they prove?
3. Why is a stale PASS dangerous after a verification failure?
4. What does a successful bounded-history linearizability test demonstrate?
5. Why does compiling a standalone consumer catch problems missed by building the library alone?
6. Why must worker threads join before the rate-limiter is destroyed?

The personal **Phase 39 PDF** is supplied separately and is intentionally *not* part of the repository.
