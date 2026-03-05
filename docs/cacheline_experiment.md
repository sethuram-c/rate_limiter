# Phase 22 - Cache-line contention and opt-in bucket isolation

The existing `lf_bucket_t` uses 24 bytes and only 8-byte alignment on the
64-bit Linux test platform. Multiple independent bucket states can occupy
one assumed 64-byte cache line. Concurrent writes to different atomics may
therefore still cause cache-line coherence traffic (**false sharing**).

We added **an opt-in experiment** rather than a default behavior change:

- Normal compilation: compact bucket, 24 bytes and 8-byte alignment here.
- `-DRL_CACHELINE_PADDED`: aligned state, 64-byte stride and 64-byte alignment.
- `ip_rate_limiter_init()` uses C11 `aligned_alloc` for padded buckets and
  ordinary `malloc` otherwise. The allocated pointer is freed normally.
- The `rate_config_plan()` payload estimate automatically reflects the build's
  `sizeof(lf_bucket_t)`. Ensure the app, library and planner use the **same**
  macro setting. Mixing ABI layouts across translation units is invalid.
- The hash index and token-bucket transition algorithm are identical in both
  variants. Both still require prepublication initialization and quiescent
  destruction, and both use the same atomic CAS ordering.

## Correctness evidence

`make cacheline-test` compiles both C11 layouts and runs 80,000 requests each
under eight workers targeting eight independent IPs. Both admit exactly
328 requests (41 tokens/IP), deny the rest and finish with zero tokens in
all eight buckets. The padded variant additionally asserts that every state
address is divisible by 64. Three Python tests validate the report arithmetic,
reject empty paired observations, and exercise real paired subprocess runs.
These tests are evidence, not a formal proof of universal lock-free behavior.

## Resource cost

A 64-slot table on the verified ABI uses key payload 64 x 8 = 512 bytes.
Compact bucket payload is 64 x 24 = 1,536 bytes (2,048 combined); isolated
bucket payload is 64 x 64 = 4,096 bytes (4,608 combined). Bucket payload grows
2.67x; the combined arrays grow 2.25x. Allocator metadata, thread stacks and
application memory remain outside this accounting.

## Sample measurements (one environment, not reproducible universal claims)

Default `make cacheline-report` runs both layouts with identical seeded streams,
matched trials, alternating order, and per-run accounting validation. The values
below were observed in one three-repeat execution using the default 60k-request
profile with unit request cost:

| Pattern | Threads | Median paired padded / compact | Interpretation |
|---------|---------|--------------------------------|----------------|
| uniform | 1 | 1.294x | padded improved this sample |
| uniform | 4 | 0.999x | effectively equivalent sample |
| uniform | 8 | 0.902x | padded slower in this sample |
| skewed | 1 | 1.054x | small sample improvement |
| skewed | 4 | 0.771x | padded much slower in this sample |
| skewed | 8 | 0.980x | near parity in this sample |

This does **not** establish false sharing as the dominant bottleneck. Small,
short microbenchmarks are noisy. Effects include CPU scheduling, hash lookups,
working-set size, hot-spot CAS contention, and cache hierarchy behavior.
The baseline remains the default because the optional padding has no robust
benefit across profiles and increases payload memory. Record measurements
from your own hardware before changing that choice.

## Linux workflow

```bash
make clean && make && make test
make cacheline-test
make cacheline-sample
make cacheline-report
column -s, -t < build/cacheline_summary.csv
lscpu | grep -E 'CPU\(s\)|Model name|L1d|L2 cache|L3 cache'
# If `perf` is available and permitted:
perf stat -e cycles,instructions,cache-references,cache-misses \
  -- ./build/realistic_traffic_bench lockfree 8 60000 uniform 64 unit 20270105
perf stat -e cycles,instructions,cache-references,cache-misses \
  -- ./build/realistic_traffic_bench_padded lockfree 8 60000 uniform 64 unit 20270105
```

Be careful with `perf`: hardware-counter permissions, event availability and
container restrictions may prevent counter collection. Generic `cache-misses`
cannot by itself prove false sharing; CPU-specific cache-to-cache tools and
controlled affinity would give more targeted evidence.

## Limitations

- The 64-byte line size is a **typical x86-64 test assumption**, not universal.
- `RL_CACHELINE_PADDED` is a **build-time ABI choice**, not a runtime toggle.
- Both layouts still contend when multiple workers CAS the **same** IP's state.
- The timed segment excludes construction and first key insertion, but includes
  request mapping, table lookup and CAS; it is not network/server throughput.
- `make cacheline-report` writes ignored CSVs to `build/`; personal learning PDFs
  belong outside the repository.
