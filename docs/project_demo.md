# Portfolio demo | 5-minute reproducible walkthrough

Purpose: demonstrate the real in-process C library, not an invented HTTP server. Start at the repository root on Linux/Fedora with GCC, Make, Python 3 and Git installed. A full `make test` run may take longer; use focused checks for a live demo.

## 0. Prepare (before presenting)

```bash
make clean
make CC=gcc library
make CC=gcc docs-test
make CC=gcc portfolio-docs-test
```

`make clean` removes the ignored `build/` directory; `make library` creates `build/libratelimiter.a`; `docs-test` stages an installation and compiles/runs a real external consumer against the installed header and static archive. `portfolio-docs-test` validates the links and claims you show during the presentation.

## 1. The story (30 seconds)

"I built an in-process IPv4 token-bucket library. Each IP receives a fixed-capacity token bucket; concurrent requests share it without overspending. The design uses C11 atomic CAS for the token/refill state, and I test it against a mutex reference under matched workloads."

Mention **version 0.1.0 Unreleased** and that it does not open sockets.

## 2. Explain the data flow (60 seconds)

Open [`diagrams/components.svg`](diagrams/components.svg) and [`diagrams/request_path.svg`](diagrams/request_path.svg). Explain the optional CIDR policy, bounded IPv4 gateway, clock source, atomic IP index, and preinitialized bucket storage.

The gateway's 200/400/403/429/503 statuses are illustrative values, not live HTTP responses. Ingress rejections can bypass the service counters.

## 3. Show the CAS invariant (60 seconds)

Open `src/bucket_state.c`, `src/lockfree_bucket.c`, and [`diagrams/cas_state.svg`](diagrams/cas_state.svg). Explain the 48-bit time/16-bit token packing, state-transition planning, retry on failed CAS, and denial/refill behavior. Distinguish the key publication CAS from the token decision CAS.

```bash
make lockfree-test
make contention-test
```

These commands validate sample concurrent executions and bounded histories, not every possible execution.

## 4. Show a real consumer and safe lifecycle (60 seconds)

```bash
make docs-test
make impossible-request-test
```

The installed-header consumer exercises version, bounded IPv4 input, quota behavior, CIDR rules and teardown. The impossible-request test verifies that never-admissible requests from unseen IPs do not consume table slots.

Open [`diagrams/lifecycle.svg`](diagrams/lifecycle.svg). State that workers must finish and join before destruction; concurrent destroy is not supported.

## 5. Show evaluation discipline (60 seconds)

```bash
make evidence-test
make evidence-smoke
cat build/evidence_report.md
```

`evidence-smoke` deliberately uses small workloads to validate the pipeline, **not** to establish a speedup. For credible measurements use `make evidence-report` separately on a controlled machine, inspect `build/evidence_summary.csv` and `build/evidence_environment.json`, and report per-workload median paired ratios with variability. Never claim "lock-free is always faster".

## 6. Finish with trade-offs (30 seconds)

Call out IPv4-only scope, a fixed-capacity insert-only index, no server, immutable policies, runtime lock-free support checks, the full service not being wait-free, and no stable pre-1.0 ABI. Point to [`architecture_decisions.md`](architecture_decisions.md).

## Troubleshooting on Fedora

```bash
cc --version
make --version
python3 --version
make help
make clean
make package-test
```

If `make docs-test` fails during staged linking, inspect the `-I.../include/ratelimiter` and archive order in `docs/developer_integration_guide.md`. To inspect symbols use `ar t build/libratelimiter.a` and `nm -g --defined-only build/libratelimiter.a`. For a test failure, rerun the focused target before concluding it is a concurrency error.
