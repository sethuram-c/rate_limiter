# Phase 35 - Reproducible benchmark evidence

## Why this exists

The repository already provides a mutex/CAS benchmark and paired CSV summaries.
Phase 35 turns those components into an **auditable, locally generated evidence
package**. We never commit machine-specific results as proof that one design is
universally faster. Run the experiment yourself on Fedora/Linux.

## Reproduce on Fedora

```sh
sudo dnf install gcc clang make python3 git
make clean
make mutex-compare-test
make evidence-test
make evidence-smoke
cat build/evidence_report.md
cat build/evidence_summary.csv
head build/evidence_raw.csv
python3 -m json.tool build/evidence_environment.json | head -50
```

For a longer experiment (not part of every CI test):

```sh
make evidence-report
# Or override the full matrix:
python3 bench/evidence_report.py \
  --benchmark ./build/mutex_compare \
  --threads 1,2,4,8 --requests 200000 --repeats 9 --warmups 2 \
  --compiler-note 'gcc -O2 -g' --output-dir build
```

`--compiler-note` records the user's declaration; it is not independently
inferred from a compiled executable. Record `cc --version`, `lscpu`, thermal
load, governor, affinity, and machine background activity separately as needed.
The script does not include hostnames or user directory names in the
human-readable report. Its environment JSON records a binary path, so review
before sharing it publicly.

## Evidence bundle (generated, ignored by Git)

| File | Description |
|---|---|
| `build/evidence_raw.csv` | Every paired warmup and measured invocation, order, accounting, elapsed ns, ops/s |
| `build/evidence_summary.csv` | Median mutex/CAS throughput; **median of paired ratios**, MAD, range and descriptive direction |
| `build/evidence_environment.json` | CPU/kernel/architecture, settings, source SHA, binary SHA256, UTC date, method/limits |
| `build/evidence_report.md` | Human-readable table, reproducibility metadata, cautions and result definitions |

If any subprocess exits with a nonzero code, times out, produces invalid
output, or reports inconsistent admission totals, the collector exits nonzero
and **does not overwrite the previous report**. Remove stale `build/evidence_*`
files before a new experiment if you want a clean empty directory on failure.
Files are staged after validation, then renamed individually; writing the four
files is not a transactional operation if the filesystem fails during output.

## Why the paired design matters

For each scenario/thread count, run both implementations at fixed simulated
time. Prepopulate identical buckets; there is no refill during the timed
interval. Rotate the order of the two implementations between repetitions.
Measure 1, 2, 4, and 8 threads separately, in shared (hot IP) and sharded
(per-worker independent IP) modes. All admissions/denials must agree.

Per measured pair: `ratio_i = CAS_ops_per_sec_i / mutex_ops_per_sec_i`.
Report `median(ratio_i)` rather than dividing two independently aggregated
medians. A ratio above one favors CAS in the sampled pair; below one favors
mutex. Median absolute deviation is `median(|ratio_i - median(ratios)|)`.
Small or mixed ratios do not support an optimization claim. Even ratios with
consistent signs do not constitute a formal confidence interval, and
benchmarks with too little measured work may be dominated by scheduler noise.

## Scope and limitations

- The benchmark is in-process: no HTTP, sockets, firewall or kernel dataplane.
- Both paths use the same atomic IP index and state transition logic; only
  packed-bucket synchronization is mutex vs CAS. This is not a fully
  mutex-based hash-table comparison.
- Worker creation, initialization and IP prepopulation are outside timing.
  Scheduling after gate release and pthread joins are included.
- Fixed simulated bucket time makes admission deterministic; the wall clock
  measures the actual duration of execution.
- Successful **and denied** requests are included in ops/s. This can measure
  a different cost profile than primarily successful production traffic.
- Compiler flags, CPU frequency, cache pressure, thermal conditions, affinity,
  VM scheduling and surrounding work may change the result significantly.
- The reporter does not establish unbiased sampling, statistical significance,
  p99 latency, network throughput or universal hardware performance.

## Debug and inspect the results

```sh
python3 -m unittest discover -s tests -p test_evidence_report.py -v
python3 -m py_compile bench/evidence_report.py
head -n 5 build/evidence_summary.csv
sha256sum build/mutex_compare
uname -srmo
lscpu | head -25
command -v taskset && taskset -pc $$
```

Use `gdb --args ./build/mutex_compare lockfree 4 100000 shared` to inspect the
existing benchmark if it fails. Do not run debug-instrumented or sanitizer
binaries as representative performance benchmarks; use them for correctness.
Keep learning PDFs **outside Git**.
