# Changelog

This document tracks meaningful source-facing milestones, not benchmark claims.
The project is educational and experimental; it has **not** been published as
an official release by this phase.

## [0.1.0] - Unreleased (development milestone)

- Reusable C11 static archive and staged installed headers, independently
  tested with an external consumer application (Phase 29).
- Lock-free per-IPv4 token buckets, configurable fixed-capacity index,
  monotonic-clock service, bounded ingress API and CIDR routing policies.
- Concurrency tests, sanitizer checks, GCC/Clang static analysis, CI workflow.
- Comparative throughput and latency benchmarks with a mutex baseline.
- Compile-time and runtime version identity and automated release preflight
  checks (Phase 30).

### Pre-1.0 limitations

- Public C structures expose implementation details; **binary ABI is not
  stable** and headers and static archive should be rebuilt together.
- Concurrency requires user-provided thread-safe clock callbacks and
  quiescent destruction after all workers have joined.
- The fixed-capacity per-IP table does not support concurrent deletion or
  resizing; entry exhaustion must be handled by the caller.
- This is an in-process library, **not** an HTTP server, proxy, or network
  identity verifier. Tests and benchmarks are not a production certification.
- No release tag, GitHub Release, binary artifact, or published version is
  implied by this changelog entry.
