# Phase 19 - Repeated-run benchmark analysis and thread scaling

This phase **does not change the C rate-limiting algorithm**. It adds a Python 3
standard-library-only statistical runner around `bench/workload_bench.c`, which
was introduced in Phase 18.

## What is actually being measured?

- `rate_service_check()` on a warm, fixed-capacity, in-process service;
- fixed *total* requests per run (not requests per thread);
- `CLOCK_MONOTONIC` measured by the existing C benchmark;
- end-to-end timing **includes pthread creation and joining**;
- no token refill during a run (`fixed_clock`), so admissions are predictable;
- shared mode contends for one IP; sharded mode gives each worker an IP;
- service-wide atomic result counters still contend in both modes.

**Not measured:** HTTP, kernel packet filtering, network RTT, p50/p99
per-request latency, and performance against a mutex baseline. Thread scaling
can be below 1 because of startup overhead, CPU contention, shared counters,
cache coherence traffic, virtualization, and frequency scaling.

## Run the experiments

```bash
make clean && make
make scaling-test
make scaling-report
```

The report executes shared and sharded workloads at 1, 2, 4, and 8 threads.
For each configuration it runs 1 discarded warmup process and 5 recorded
processes, rotating the configuration order between passes. Each recorded
process executes **200,000 total requests**. Exactly 40 recorded runs and 8
warmups are expected. The report creates:

- `build/scaling_raw.csv`: one row per recorded run including elapsed time,
  calculated unrounded throughput, and exact allow/deny/error accounting;
- `build/scaling_summary.csv`: one row per configuration with robust aggregates,
  speedup and efficiency.

`build/` is ignored by Git. Do not commit machine-dependent measurements as
universal performance claims.

Customize:

```bash
python3 bench/scaling_report.py \
  --benchmark ./build/workload_bench \
  --scenario both --threads 1,2,4,8,16 \
  --requests 1000000 --repeats 7 --warmups 2 \
  --raw-csv build/my_raw.csv --summary-csv build/my_summary.csv
```

Scripts require Python 3.10+ and no third-party packages. The benchmark binary
must exist. Invalid thread lists, malformed benchmark output, nonzero exit,
`valid=no`, missing fields, negative counts, nonpositive elapsed time, and
inconsistent request totals cause the tool to fail rather than publish data.

## Statistical definitions and interpretation

Let `T` be the fixed total request count, and `dt_ns` be the C benchmark's
reported monotonic elapsed nanoseconds. The runner recomputes

    ops/s = T * 1,000,000,000 / dt_ns

from exact integer timings rather than the rounded `ops_per_sec` printed by C.
For repeated rates `r1..rn`, it reports:

- **median:** median rate (robust typical value);
- **p10 / p90:** linearly interpolated 10th / 90th percentiles of *run-level
  throughput samples*, NOT per-request latency percentiles;
- **MAD:** median of `abs(ri - median)`; a robust variability indicator;
- **speedup(t):** median throughput at `t` threads / median at one thread,
  computed separately for shared and sharded cases;
- **efficiency(t):** `speedup(t) / t`. Values can exceed 1 for noisy runs.

Example math: if one thread has a median of 10 Mops/s and four threads have
30 Mops/s, speedup is `3.0x` and efficiency is `3/4 = 75%`. If four-thread
throughput falls to 8 Mops/s, speedup is `0.8x`, not an error: contention and
overhead can dominate.

**Caution:** Five runs are only exploratory, not a confidence interval. P10 and
P90 from five samples should not be called statistically stable. Use 20-30+
runs and longer workloads for external claims, isolate the CPU where
practical, and include the exact machine, Linux kernel, compiler, frequency
policy, CPU affinity, processes sharing the host, and command line.

## Linux performance inspection

```bash
uname -a                             # kernel and architecture
lscpu                                # logical CPUs and topology
nproc                                # CPUs available to this shell
command -v taskset && taskset -pc $$ # current affinity (where available)
/usr/bin/time -v ./build/workload_bench 4 1000000 shared
```

If `taskset` is available, you may keep the entire Python runner within a
known CPU set with `taskset -c 0-3 python3 bench/scaling_report.py ...`.
Only choose CPUs actually allowed by your current environment (inspect
`taskset -pc $$`). This does **not** guarantee a noise-free machine. `perf stat`
may require Linux permissions; permission failures are not benchmark failures.

```bash
perf stat -e cycles,instructions,cache-misses \
  ./build/workload_bench 4 1000000 shared
```

The tool checks **correctness** before accepting a performance sample.
Run the full regression suite independently:

```bash
make test
python3 -m unittest discover -s tests -p 'test_scaling_report.py' -v
```

Tests cover percentile math, MAD, speedup, invalid output/accounting,
argument parsing, subprocess errors, and a real small end-to-end CSV run.

## Reproducibility pitfalls

1. Do not compare results obtained with different compiler flags, workloads,
   number of IPs or request counts.
2. Do not infer p99 latency from throughput, or infer linearizability from a
   benchmark. Concurrency correctness has separate tests in earlier phases.
3. Distinguish *fixed total work* from *fixed work per thread*: they answer
   different scaling questions.
4. Ignore a few astonishingly fast/slow runs until they are replicated.
5. The baseline is the *same scenario* at one thread; never compare sharded 8
   threads with shared 1 thread and call it speedup.
6. A sharded workload still shares service-level atomic counters and hash
   infrastructure; it is not a perfectly independent-core workload.
7. A `valid=yes` output is necessary, not a proof that the benchmark precisely
   isolates all intended costs.

## Git boundary and one-hour acceptance criteria

Keep the **Phase 19 personal learning PDF outside the repository**. The code
snapshot includes only source, repository documentation, Makefile, and tests.
To finish, confirm:

- `make scaling-test` passes; `make test` passes;
- `make scaling-report` writes exactly 40 raw and 8 summary records (excluding
  CSV headers), using default arguments;
- every raw row has nonzero throughput, `allowed + denied == requests`, and
  `errors == 0`;
- speedup for one thread is exactly 1.0 and efficiency there is 1.0;
- invalid benchmark results fail fast with a clear error;
- `git diff --check` and `git status` show only intentional files.

Next planned phase: build and measure a **mutex-protected comparison baseline**
with identical workload semantics before making performance claims.
