# Phase 33 - Reliability hardening: impossible requests and IP-slot exhaustion

## The failure mode

The limiter uses a bounded, never-evicted open-addressed IP table. Before this
change, a new IP claiming more tokens than bucket capacity was inserted and
then correctly denied. Repeating that request with different IP keys could
consume the whole table even though none of those requests could ever succeed.
With a capacity-2 bucket and only 2 IP slots, just two impossible requests
could prevent a legitimate third IP from being admitted.

## New behavior

* Input amount 0 or invalid `now_ms`: `INVALID`, no new slot (unchanged).
* `1 <= amount <= bucket.capacity`: ordinary find/insert and CAS update
  (unchanged). An unseen key may occupy an index slot even if denied due to
  temporary quota exhaustion, because it could be admitted after refill.
* `amount > bucket.capacity`, unseen IP: `DENIED`, **no new slot claimed**.
  There is no committed bucket state or retry hint for this decision. The
  service still counts one denial; a syntactically valid gateway request still
  forwards and returns HTTP-style 429.
* `amount > bucket.capacity`, existing IP: retain the usual bucket transition
  and CAS (or mutex in the comparison baseline), preserving refill and
  decision-aligned diagnostics for already-registered clients.
* When the table is full, an impossible request for an unseen IP is `DENIED`
  rather than `TABLE_FULL`. It cannot be admitted irrespective of capacity.

`IP_RATE_DENIED` therefore does **not** imply a committed bucket state for
all requests. Callers must inspect `report.valid_state` or
`diagnostic.has_bucket_state` before reading state metadata.

## Concurrency and linearization

The index is append-only, with no deletion or relocation during its lifetime.
A `find()` for an oversized request performs atomic reads and never writes.
If it observes the key, the existing bucket follows its normal synchronized
update path. If it sees the key as absent, an immediate denial is correct:
there were never enough tokens for this cost even if another thread inserted
the key at the next instant. Ordinary first-time requests still use the
existing CAS insertion path, and the bucket policy is immutable after init.

The mutex comparator is updated to match these admission/resource semantics;
otherwise correctness benchmarks would compare different policies.

## Explicit limitations

This is **not** an IP-eviction algorithm or full DoS mitigation. Requests
within the configured bucket capacity can still claim every fixed-capacity
slot; deployments must choose limits and upstream controls accordingly. The
rate-service clock is still read before the per-IP decision, and the callback
is responsible for its own progress guarantee. A CIDR bypass rule is a
separate ingress policy, not governed by token costs in the bucket.

## Verification

Run on Fedora/Linux:

```bash
make clean
make impossible-request-test
make mutex-test
make request-gateway-test
make rate-diagnostics-test
make static-check
make test
```

The focused test checks 10,000 sequential oversized IPs and 40,000 concurrent
oversized requests from 8 workers without index growth; a valid request then
successfully claims a slot. It checks existing-IP refill and committed state,
table-full precedence, service counters, HTTP-style 429, and no retry hint.
Compiler/sanitizer success does not formally prove all interleavings safe.

The Phase 33 personal learning PDF is stored outside Git.
