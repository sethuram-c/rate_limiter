# Phase 20 - Controlled mutex versus CAS experiment

## Purpose and scope

This benchmark compares the **per-IP bucket synchronization strategy**, not two
independently implemented end-to-end systems. Both variants share:

- `ip_atomic_table_t`: same open-addressed insertion/lookup operations;
- `bucket_policy_t` and `bucket_state_plan()`: same refill/token transition;
- fixed table size (64 slots), same IPv4 keys, same initial time and budget;
- no allocating, IO, logging, sleeping, or clock calls inside the request loop;
- the same C benchmark executable, loop structure and worker start gate.

The lock-free version (`ip_rate_limiter_check`) commits with one atomic CAS loop.
The baseline (`mutex_rate_limiter_check`) guards each packed state using a
`pthread_mutex_t`. The baseline is a **hybrid**: its IP index is still lock-free,
and bucket updates are mutex-protected. The presence of a mutex means the full
baseline request path is *not* lock-free. This isolates synchronization costs,
but does not prove that a fully mutex-based hash table would behave similarly.

## Correctness first

- Test that both implementations produce identical decisions for deterministic
  multi-IP, refill, full-table, invalid, oversize, and stale-time timelines.
- Test under contention: eight threads, 10,000 requests each, four IPs, 37
  tokens/IP, fixed timestamp: exactly 148 admissions per implementation.
- All requests use one token; no refill occurs during benchmark timing.
- Each benchmark process prepopulates the required IP keys and consumes **one
  warmup token per key**. With 30,000-token capacity, each IP has 29,999 left.
- A run is rejected unless `allowed == expected`, `denied == requests-expected`
  and `errors == 0`. `valid=yes` means internal accounting checks passed.
- For `shared`, `expected = min(total_requests, 29999)`.
- For `sharded`, `expected = sum(min(worker_requests_i, 29999))`.

## Design: initialization, lock and lifetime

The mutex implementation preinitializes every mutex and packed bucket before
any IP is published. A thread finds/claims a stable IP slot via the existing
atomic index; the selected bucket's mutex serializes `bucket_state_plan()` and
assignment. All changes to `mutex_bucket_t.state` happen with its mutex held.
On failure, the initializer releases already-created mutexes and frees the index.
Do not initialize/destroy while worker threads are active; all threads must join
before calling destroy. `pthread_mutex_lock` can block or sleep under contention.

The lock-free variant uses the packed 48-bit timestamp/16-bit token state and
atomic compare-exchange. Its successful CAS is the bucket linearization point.
The mutex variant's state assignment under an exclusive mutex is its update
point. Same-token-count admission does not make their **ordering** identical:
concurrent operations can interleave differently and still be correct.

## Reproducible commands

```sh
make clean && make
make mutex-test
make mutex-compare-test
make mutex-compare-sample
make test
make mutex-compare-report
cat build/mutex_comparison_summary.csv
head build/mutex_comparison_raw.csv
```

Manual paired runs:

```sh
./build/mutex_compare mutex 4 200000 shared
./build/mutex_compare lockfree 4 200000 shared
./build/mutex_compare mutex 4 200000 sharded
./build/mutex_compare lockfree 4 200000 sharded
```

Override the benchmark runner:

```sh
python3 bench/mutex_compare_report.py \
  --benchmark ./build/mutex_compare \
  --threads 1,2,4,8 --requests 200000 \
  --repeats 5 --warmups 1 --output-dir build
```

## What is actually timed?

Worker threads are created **before** starting the timer, spin on an atomic
start gate outside the request path, and are released after `CLOCK_MONOTONIC`
starts. The timer ends after every worker is joined. Hence this measurement
includes worker scheduling/join tail latency and request processing; it
excludes process startup, heap allocation, key prepopulation and pthread
creation. This is NOT the same timed region as the Phase 18 benchmark, which
includes thread startup. Do not compare Phase 18 and Phase 20 values directly.

`shared`: every worker accesses one IP/bucket - worst-case contention.
`sharded`: each worker has a unique IP/bucket - lower contention. Every worker
still uses the same 64-slot hash index (and probes its own key).

## Statistics and honest interpretation

`mutex_compare_report.py` executes warmups and measured **pairs**. It reverses
the A/B run order on alternate repetitions, parses strict key=value records,
and writes individual samples and median summaries to ignored `build/*.csv`.
The paired ratio is `throughput_lockfree / throughput_mutex` for each repetition,
then the **median of those ratios**. A value >1 favors lock-free in this
specific benchmark; <1 favors mutex. The ratio is not necessarily the ratio
of the separate medians, and neither is an architectural guarantee.

Re-run on a quiet machine, repeat across multiple sessions, record CPU model,
compiler/flags and thermal/scheduler conditions before quoting performance.
Thread pinning (`taskset`) and `perf stat` are optional exploratory tools:

```sh
uname -a
lscpu | sed -n '1,30p'
cc --version | head -1
nproc
command -v taskset && taskset -c 0-3 ./build/mutex_compare mutex 4 200000 shared
command -v perf && perf stat -e cycles,instructions,cache-misses \
    ./build/mutex_compare lockfree 4 200000 shared
```

Counters/CPU affinity may require permissions and some systems lack 4 usable
CPU ids; check `taskset -pc $$` for your allowed CPU mask before pinning.

### Confounds and limits

- Different object layouts and mutex metadata create different cache behavior.
- A mutex may outperform a CAS loop under some contention patterns; kernel
  scheduling and cache invalidations change results.
- The fixed clock means no refill or OS clock cost during the benchmark.
- All outcomes are counted; after exhaustion, denial still performs a state
  update and contends. This is a *synthetic fixed-time* workload.
- This is an in-process IP request API; not an HTTP server, network stack,
  end-to-end latency test, proof of lock freedom on every target, or steady
  production load.
- Matching outcomes verifies budget accounting, not all possible concurrent
  histories; earlier short-history checks provide additional bounded evidence.
- `valid=yes` is a correctness/accounting flag; it does not mean a run is
  statistically stable or that either variant is universally faster.

## Debugging and verification

```sh
# strict, fresh build
make clean && make CC=gcc CFLAGS='-std=c11 -Wall -Wextra -Wpedantic -Werror -O0 -g'
make mutex-test mutex-compare-test

# UBSan (separate build; do not mix with other sanitizer objects)
make clean
make CC=gcc CFLAGS='-std=c11 -Wall -Wextra -Wpedantic -Werror -O1 -g -fsanitize=undefined -fno-omit-frame-pointer' LDFLAGS='-fsanitize=undefined' mutex-test mutex-compare-test

# ASan (separate build)
make clean
make CC=gcc CFLAGS='-std=c11 -Wall -Wextra -Wpedantic -Werror -O1 -g -fsanitize=address -fno-omit-frame-pointer' LDFLAGS='-fsanitize=address' mutex-test

# restore normal build for performance runs
make clean && make mutex-test mutex-compare-test

gdb --args ./build/mutex_compare mutex 4 10000 shared
# gdb: break mutex_rate_limiter_check; run; info threads; bt; continue
```

Do not benchmark sanitizer builds as if their timing were normal optimized C.
ThreadSanitizer is a valuable optional check on a supported host but can fail
because of sanitizer/runtime incompatibility; report unavailable, not passed.

## Review questions

1. Why is a mutex baseline useful even when the main goal is lock-free design?
2. Which parts of the two implementations are exactly shared?
3. Does the comparator isolate the cost of a completely mutex-only hash table?
4. How does the mutex version avoid data races on packed bucket state?
5. Why initialize buckets before publishing keys?
6. Why are request counts correct despite nondeterministic admission order?
7. How can a ratio based on just one sample be misleading?
8. Why might sharded workloads still contend in the CPU cache hierarchy?
9. How does the start gate differ from including `pthread_create()` in timing?
10. What would change to benchmark refill or varying IP distributions?
