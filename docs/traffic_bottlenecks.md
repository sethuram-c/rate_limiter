# Phase 21: realistic traffic profiles and bottleneck diagnosis

The goal is to benchmark **the same per-IP rate-limiter interface** under
multiple deterministic request distributions, without changing the production
bucket or table implementations. This is an in-process microbenchmark, not an
HTTP service throughput test or a formal proof of scalability.

## Reproducible workload matrix

- `hot`: all requests target one IP and contend on one bucket.
- `uniform`: deterministically distributes requests across N IP addresses.
- `skewed`: 80% select from the first four IPs, 20% choose from the full N.
  This means the first four IPs receive a little more than 80% overall.
- `unit`: every request consumes one token.
- `mixed`: approximately 80% consume one token, 20% consume three tokens.
- `seed`, `worker_id`, `iteration` select an IP and cost deterministically.
  There is no shared random-number generator or mutable RNG lock.

IPv4 keys are synthetic `198.18.0.0/15` benchmarking keys, not real user data.
A 128-token bucket is fully initialized for every key, then one token is
consumed during prepopulation. The fixed logical clock prevents refills during
timed work. Both implementations share the same lock-free IP index, the same
state-transition algorithm, and the same prepopulated key set. Only per-bucket
synchronization differs (pthread mutex vs 64-bit atomic CAS).

## Correctness under mixed-cost concurrency

`allowed + denied + errors == requests`; `errors == 0`.
After all workers join, for each IP:

    remaining_tokens[ip] + total_successful_token_cost[ip] == 127

and `total_successful_token_cost[ip] <= 127`.

The experiment tracks allowed **token cost**, not just allowed request count.
A mutex and CAS run can admit different numbers of requests of different costs
without any correctness violation. Do not compare allowed-request totals as
if they must be identical under a nondeterministic schedule. Prepopulation
costs one token per IP, outside the timer. No refill occurs while measuring.

## Measurement boundaries

The timer starts when a pre-created group of threads is released, ends after
all worker joins; startup itself is excluded but thread wakeup and joining
are included. The timed loop performs the deterministic IP/cost selection,
atomic index lookup, bucket update and local statistics, not just the CAS or
mutex primitive. Per-worker admission tallies avoid an extra shared counter
in the request hot path. Final snapshots and validation are outside the timer.
All reported throughput is `requests / wall_time_seconds` (attempts per
second, **not** successful grants per second and **not** HTTP RPS).

## Commands

```sh
make traffic-profile-test
make traffic-profile-sample
make traffic-profile-report
./build/realistic_traffic_bench lockfree 8 200000 skewed 64 mixed 20270104
./build/realistic_traffic_bench mutex 8 200000 skewed 64 mixed 20270104
python3 bench/traffic_profile_report.py --threads 1,4,8 --ips 64 --requests 100000 --repeats 5 --warmups 2
```

CSV outputs (ignored by Git) are `build/traffic_profile_raw.csv` and
`build/traffic_profile_summary.csv`. Paired runs share the same seed and the
order reverses between repetitions to reduce systematic drift. Report both
per-implementation median throughput and median paired LF/mutex ratio.
Ratios above 1 indicate LF faster **only under the measured configuration**.

## Diagnostic flow when throughput declines

1. Confirm valid=yes, request accounting, memory budget, no clock changes.
2. Repeat multiple times with warmups; note CPU type, frequency scaling,
   compiler, build flags, thread count and affinity.
3. Compare hot vs uniform: a larger hot slowdown suggests contention on a
   small number of bucket cache lines. If uniform is also slow, assess atomic
   hash lookup, scheduler overhead, RNG mixing, and capacity/cache footprint.
4. Compare 1 vs N threads and inspect per-run variance, not just median.
5. Optionally inspect `perf stat -e cycles,instructions,cache-misses` and
   `perf record -g` **if perf is installed and access is permitted**. Lack of
   permission is not evidence of a benchmark failure.
6. To measure raw bucket synchronization alone, create a separate benchmark
   eliminating key hashing and workload generation. This phase measures a
   complete per-IP in-memory path, so do not attribute all slowdown to CAS.

## Known limitations

Synthetic fixed-clock workload and short runs; small fixed-capacity map,
no IP eviction, no table resizing, no HTTP parsing or sockets, no true client
arrivals, no percentiles of per-request latency. The worker ID contributes to
the request stream; changing thread count therefore changes the generated
traffic as well as parallelism. Pairs at the same thread count share the same
stream. Scheduling changes which mixed-cost requests win; successful request
counts can differ despite correct token consumption. Lock-free does not mean
wait-free. Do not claim universal performance superiority or production RPS.
