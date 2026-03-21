# Phase 38: Production assumptions, limitations and residual-risk review

**Assessment: NOT PRODUCTION READY.** This is a fixed-capacity, in-process, IPv4-only C11 library for systems learning and integration demonstrations; it is not a deployed HTTP service. A passing **risk-document completeness check** does not imply operational safety. No new performance claims, product release, or new runtime feature are introduced in Phase 38.

## Reading the risk register

[`production_risks.json`](production_risks.json) is the machine-checked source of the twelve identified risks. Each record includes a concrete failure mode, operational mitigation, implementation or design evidence, a test reference, and an executable Makefile test target. `blocker` means a production deployment must resolve the corresponding external capability or architecture gap. `accepted-limitation` means the limitation is explicit, not that it is safe in every environment.

| ID | Scope / failure mode | Disposition | Required response |
|---|---|---|---|
| R01 | Caller-supplied client identity can be spoofed | Blocker | Validate the trust boundary outside the library |
| R02 | Distinct valid-sized IPs can fill the fixed table | Blocker | External admission control, monitoring, future reclamation design |
| R03 | Concurrent destroy can use-after-free | Blocker | Stop ingress and join workers before destruction |
| R05 | No socket server, TLS or HTTP protocol implementation | Blocker | Integrate with a separately validated network server |
| R04/R07 | Clock correctness and atomic/range requirements | Limitation | Validate callback and platform capabilities |
| R06/R08 | ABI evolution and non-transactional live stats | Limitation | Pin builds; quiesce for exact aggregate accounting |
| R09/R10 | Workload-specific performance and immutable policies | Limitation | Measure and manage configuration off the request path |
| R11/R12 | IPv4-only scope and bounded verification evidence | Limitation | Document scope and continue concurrency audit |

## Guarantees, boundaries and non-guarantees

1. **Index and bucket concurrency:** the IP slot key and each packed bucket word use separate atomic operations; a request has a single bucket-state CAS linearization point, but there is no global transaction across all IPs or counters.
2. **Lifetime:** index slots and bucket backing memory are created before worker publication. Neither deletion nor concurrent reclamation is implemented. An owning application must join all workers before `destroy`.
3. **Capacity:** configurations limit the array payload; they do not bound total process memory, nor do they prevent valid-sized requests from many distinct IPv4 addresses filling the insert-only index.
4. **Client identity and protocols:** the gateway parses IPv4 strings and returns HTTP-style numbers. It does not authenticate forwarded headers, listen on TCP, parse HTTP or perform TLS.
5. **Clock:** the service calls a POSIX monotonic clock or a supplied callback before claiming new slots. A slow callback can make the whole service slow even if the bucket CAS itself is lock-free.
6. **Observability:** admission diagnostics correspond to a particular CAS decision. Five statistics fields loaded concurrently do not form an atomic cross-field snapshot.
7. **Portability:** the 48-bit timestamp / 16-bit token state limits ranges; 64-bit atomics must be lock-free on the target. Compile all modules consistently when optional cache-line padding is enabled.
8. **Performance:** comparisons to the mutex baseline must use paired local evidence; do not claim generic latency or throughput improvements.
9. **Release:** version 0.1.0 is unreleased, and visible struct layouts are not a stable ABI guarantee.

## Fedora / Linux verification

```bash
make risk-audit-test      # tests the checking tool, including deliberate failures
make risk-audit           # checks every risk, evidence path and Makefile test target
python3 tools/risk_audit.py --summary
make quality-quick        # fast compiler and correctness verification
make test                 # full regression suite, including risk-audit-test
```

### Expected negative demonstration (do not treat failure as a broken build)

```bash
python3 tools/risk_audit.py --strict-production
# exits 3, with a clear list of unresolved blockers; this is intentional.
```

**Important:** `make risk-audit` exits 0 only if the review records are internally consistent and grounded in repository evidence. It does *not* greenlight deployment. `--strict-production` intentionally rejects this repository while the listed blocker risks remain unresolved.

## Scope for a future deployment review

Before advertising production suitability, identify authenticated client identity handling, an eviction strategy or external front-door capacity defenses, thread ownership and shutdown guarantees, transport/TLS/HTTP implementation, structured runtime telemetry and alerts, operational SLOs, long-running load tests, and actual platform CI status. Reassess all risks after every architectural change. These are proposed future efforts, **not implemented features**.

## Review exercises

1. If 200,000 unique IPv4 addresses send valid-sized one-token requests, why can the table fill even though oversized-request protection is present?
2. Can the C11 atomic counter snapshot be treated as a globally linearizable snapshot? Why not?
3. What additional proof would be required before deleting an idle IP while workers access the table?
4. Would `atomic_is_lock_free` being true guarantee a lock-free service API? Explain clock callbacks.
5. Which two independent CAS transitions may occur for the first request from a previously unknown address?

Answers: (1) insert-only capacity is consumed per unique IP; (2) no, loads occur at different times; (3) a correct reclamation and synchronization design; (4) no, the callback can block; (5) key-slot publication and token-state update.

The Phase 38 personal learning PDF belongs **outside Git**. Read [`architecture_decisions.md`](architecture_decisions.md), [`public_api_reference.md`](public_api_reference.md), and [`release_quality_gates.md`](release_quality_gates.md) for deeper implementation and verification details.
