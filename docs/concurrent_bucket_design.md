# Concurrent token bucket: state representation (Phase 6)

This is a design note, not a claim that the token bucket is thread-safe yet.

## Why tokens and refill time must be updated together

A refill changes both the available balance and the timestamp anchor. If they
are stored in separate atomics, other threads may observe impossible mixes:
new tokens with an old anchor can refill twice, or a new anchor with an old
balance can lose credit. The sequential model already treats these fields as
one transition. The concurrent implementation must preserve that invariant.

## One-word proposal

A 64-bit state word contains a **48-bit monotonic millisecond anchor** in the
most significant bits and a **16-bit token balance** in the least significant
bits. Limits are explicit: capacity 1..65535; refill interval 1..2^48-1 ms;
caller timestamps 0..2^48-1 ms. The timestamp range spans about 8,900 years.
We lose sub-millisecond precision relative to the nanosecond Phase 4 API.

```
 bit 63                              bit 16 bit 15       bit 0
 +-----------------------------------------+------------------+
 |            last_refill_ms (48b)          |   tokens (16b)   |
 +-----------------------------------------+------------------+
```

`bucket_state_plan()` accepts a policy, observed packed state, requested amount
and sampled `now_ms`. It returns a proposed packed state and allow/deny outcome.
It does not mutate shared state. Refill advances only through complete periods;
stale timestamps never move the anchor backward; a denied request may advance
the anchor while accruing tokens. Tests compare 24,000 transitions with the
existing sequential token-bucket reference.

## Planned Phase 7 CAS sketch (not implemented here)

```
observed = atomic_load_explicit(&bucket->state, memory_order_relaxed);
for (;;) {
    if (!bucket_state_plan(&bucket->policy, observed, amount, now, &step))
        return ERROR;
    if (atomic_compare_exchange_weak_explicit(&bucket->state,
            &observed, step.next_state,
            memory_order_relaxed, memory_order_relaxed))
        return step.allowed ? ALLOW : DENY;
}
```

**Why CAS must succeed before returning a result:** even an unsuccessful
request can refill the snapshot. A losing CAS attempt must replan using the
winning state; otherwise two requests might both be permitted against a stale
balance. The successful CAS is the intended linearization point. A same-value
CAS is permitted; its success still provides a linearization point.

## Concurrency contract and risks to test later

- Bucket policy and storage must be initialized before threads start and must
  not be mutated or freed until workers have stopped.
- `atomic_uint_fast64_t` is at least 64 bits, not necessarily exactly 64. We
  should explicitly check `atomic_is_lock_free` on supported platforms and
  document that portability can limit a *lock-free* claim.
- Reads of the state must be atomic. Copying a `uint64_t` snapshot between
  threads without atomic synchronization would introduce a data race.
- A CAS retry loop gives lock-free algorithmic progress only when underlying
  atomics are lock-free, threads are scheduled, and policy stays immutable;
  an individual worker may starve.
- Clock samples can be stale relative to updates by other threads. The state
  planner clamps stale time to the stored anchor, avoiding time reversal.
- Repeated CAS conflicts can be expensive; benchmark against a mutex baseline.
- No deletion, resize, per-IP table, or concurrent lifetime management exists.

## Commands

```
make state-test
make state-demo
make test
```

Personal phase learning PDFs are NOT part of this repository.
