# Project discussion notes

**Claim discipline:** These statements refer to the checked-in **in-process C11 library**, not a production web service. Version **0.1.0 remains unreleased** and has explicit deployment blockers. Replace no placeholder with an invented speedup or throughput number.

## Short project description

**Lock-Free Per-IP Rate Limiter | C11, POSIX Threads, Linux, Atomics, CAS**

- Built a per-IP token-bucket rate limiter in C11 using a fixed-capacity concurrent IPv4 index and packed 64-bit atomic state, with CAS-based admission and configurable refill/burst limits.
- Verified concurrency and failure behavior with deterministic property tests, pthread contention tests, bounded-history linearizability checking, allocation-failure injection, and GCC/Clang tooling.
- Benchmarked CAS against a mutex-based reference under matched workloads, producing reproducible throughput/latency reports and documenting cases where synchronization choices trade off differently.

Choose two or three bullets that fit your actual contribution. Do not claim a network proxy or universal performance win. If you quote a measured number, include the test parameters and machine configuration elsewhere in the portfolio.

## Thirty-second explanation

"I built an in-process per-IP token-bucket limiter in C11. It maps IPv4 clients to preallocated buckets and packs refill time plus tokens into a single 64-bit atomic word, updated with compare-and-swap. That prevents concurrent requests from overspending a bucket. I tested it with pthread contention and an independent reference model, then compared its behavior and performance to a mutex implementation. It is a documented systems library, not a deployed HTTP server."

## Ninety-second architecture walk-through

The gateway validates IPv4 text, and an immutable CIDR policy may bypass or block a request. The service obtains monotonic time and finds the client's slot in a fixed-size atomic key index. Bucket state is initialized before the key is published. The request loads the packed token/refill word, computes a transition, and attempts a CAS; if another thread wins, it must reload and recompute. This bucket CAS is a different operation from key publication. The service returns allow/deny or an explicit error, and updates atomically counted outcomes. Correctness depends on quiescent destruction and honest table/clock contracts. The fixed-capacity design avoids hot-path allocation but lacks eviction.

## Five-minute live demonstration

```bash
make library
make package-test
make contention-test
make impossible-request-test
make rc-demo
python3 -m json.tool build/rc_verification.json
```

Show `docs/diagrams/components.svg` and `docs/diagrams/cas_state.svg` while speaking. Describe the final `rc-demo` file as a *demo profile* rather than claiming it is identical to `rc-check`. Point out the production risk register.

## Interview questions and grounded answers

**1. Why pack timestamp and tokens into one atomic?** Because updating separate atomics independently could permit a logically inconsistent refill/consume transition. A single CAS commits the two fields together.

**2. What is the linearization point?** For a token-state decision that updates state, the successful bucket CAS; distinct first-time key registration uses a separate key-publication CAS. Explain unchanged-state denial behavior based on the implemented function rather than pretending every response always writes.

**3. Why does a failed CAS need a recompute?** Another thread may have changed time/token state. Blindly retrying an old computed value would lose updates or spend tokens against a stale balance.

**4. Is the whole system lock-free?** No blanket guarantee. The single-bucket atomic update targets lock-free progress when 64-bit atomic support is verified. Callbacks, allocator/lifecycle code, tests and process scheduling can block.

**5. How is memory reclamation handled?** Entries are insert-only and arrays are preallocated. Callers must stop intake and join workers before destroying the service; it deliberately does not solve concurrent reclamation.

**6. Why benchmark against a mutex?** Lock-freedom describes progress, not universal speed. The matched reference helps isolate bucket synchronization from hashing and transition policy. Compare workloads, warmups, medians and tail latencies before drawing conclusions.

**7. What does the bounded linearizability checker establish?** It checks recorded short concurrent histories and rejects impossible negative controls. It is evidence, not a proof for all schedules or every public operation.

**8. Can an attacker avoid the limiter by spoofing IP addresses?** The library does not authenticate the caller-supplied IP. The embedding application must enforce the trust boundary. This is a documented production blocker.

**9. Why is there no eviction or resize?** It avoids complicated reclamation on a hot concurrent path, with the trade-off of fixed capacity and `TABLE_FULL` for unseen keys after exhaustion.

**10. Does a clean sanitizer run prove race freedom?** No. Sanitizers can detect issues in exercised paths. Tests, state invariants, memory-order arguments and clear lifecycle assumptions are complementary.

**11. How would you add IPv6 or safe eviction?** First revisit key storage and lifetime. Variable-width keys, slot migration and memory reclamation are nontrivial; a design and new tests would precede implementation.

**12. Why is version 0.1.0 unreleased?** Packaging, reproducibility and automated checks exist, but the library still has explicit deployment blockers and an unpromised public ABI.

## Common misunderstandings

- Do not call `429` output a real HTTP response: the gateway is an in-process adapter.
- Do not call CAS wait-free: individual threads may retry indefinitely.
- Do not report all project lines at 100% coverage: the documented GCOV probe covers only the sequential token bucket.
- Do not report the rate limiter as immune to table exhaustion: valid-sized requests from distinct IPs can still fill it.
- Do not call the 12 risk entries resolved: four remain production blockers.
- Do not state that hosted GitHub Actions passed unless you actually checked the remote job results.

## Review checklist

- [ ] I can explain every field in the 64-bit bucket state and why CAS is required.
- [ ] I can trace first-time insertion to an initialized bucket.
- [ ] I can reproduce the installed-library consumer on Fedora.
- [ ] I can run `make rc-check` and interpret the exit code/JSON correctly.
- [ ] I can describe four deployment blockers and the no-concurrent-destroy rule.
- [ ] I can show raw benchmark files before citing performance.
- [ ] I have pushed to the intended Git remote and separately verified CI, if publishing.

The implementation is complete for its current experimental scope, with the deployment limitations noted above.
