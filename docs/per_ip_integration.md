# Phase 11 - Safe concurrent per-IP token buckets

## Overview

`ip_rate_limiter_t` composes the existing fixed-capacity atomic IPv4 key
index with one `lf_bucket_t` for every physical slot. No new bucket is
allocated or initialized while a worker is admitting requests.

```text
BEFORE worker start (quiescent):
  allocate atomic IPv4 slots -> init to EMPTY
  allocate bucket array      -> initialize every bucket with one policy
  publish initialized limiter to workers through normal thread start

DURING concurrent request:
  validate input -> atomically find/claim IP slot -> consume from bucket[index]
                                                   via 64-bit CAS
```

## Why preinitialization is necessary

Incorrect:

1. Thread A claims an empty key slot by CAS (key becomes visible).
2. Thread B sees the same key and tries to consume from its bucket.
3. Thread A is still calling `lf_bucket_init()` on the payload.

Step 2 reads ordinary policy fields and an atomic state before initialization,
which is undefined behavior. A key CAS is **not** a reservation protocol for
payload initialization. Phase 11 avoids the problem entirely by initializing
*all* per-slot bucket payloads before making the limiter accessible to any
worker. Thread creation then establishes the needed handoff from initializer
to workers. Later, the slot key can be safely claimed by CAS because its
corresponding bucket is already initialized.

The first winning insertion selects a dedicated bucket that no other key can
ever own. Duplicate same-IP callers observe the same stable slot and contend
on that bucket's CAS; distinct IPs select distinct slots even after collisions.

## Concurrency and linearization

- Atomic key insertion: successful strong CAS from empty to tagged IPv4.
- Existing key lookup: sequentially consistent observation of tagged IPv4.
- Token decision: successful weak CAS updating packed tokens and refill anchor.
- A denied request may still CAS an updated refill state.
- Distinct IPs cannot consume the same bucket's tokens.
- A full index returns `IP_RATE_TABLE_FULL`, never an ordinary DENIED result.
- Invalid amounts and out-of-range timestamps are rejected **before** key
  insertion, leaving the table unchanged.

The key insertion and token decision are *two separate linearization points*.
The public `ip_rate_limiter_check` operation is intended to mean "find or
create the bucket for this IP, then perform one atomic token decision". The
first encounter with a new key exposes that key before its first consumption,
which is safe because the bucket already exists, but callers should not
claim that the whole composite operation has one CAS linearization point.

## Progress and limitations

- Key probing scans at most `capacity` slots. It never allocates or locks.
- Bucket consumption uses an unbounded retry CAS loop, and is lock-free as
  a system-wide progress guarantee, **not** wait-free per requesting thread.
- The implementation checks 64-bit atomic lock-freedom at initialization;
  not every CPU/ABI supports it.
- No hash-table deletion, resizing, concurrent reinitialization, safe memory
  reclamation, dynamic per-IP policies or network integration exists yet.
- Time is injected as monotonic milliseconds, 0 to 2^48 - 1. Refill policy is
  one token per configured interval. Capacity is between 1 and 65535.
- Allocation is done during setup; the overall *lifecycle* is not lock-free.
- Readers must not race with destroy. Workers must be joined first.
- The input IP is a canonical `uint32_t`. Address parsing and network-byte
  order conversion belong at a future API boundary.

## Reproduce verification

```sh
make clean && make && make ip-rate-test && make ip-rate-demo && make test
make clean && make CC=clang && make CC=clang test
make clean && make CC=gcc CFLAGS='-std=c11 -Wall -Wextra -Wpedantic -Werror -O1 -g -fsanitize=undefined -fno-omit-frame-pointer' LDFLAGS='-fsanitize=undefined' test
```

12 pthread workers contend for one IP (90,000 requests; exactly 173 admitted)
and later for 64 IPs (76,800 requests; 2,368 admitted). The tests also cover
IPv4 zero and max, refill boundaries, invalid inputs, slot saturation,
bucket independence, lifecycle, and coherent snapshots.

These tests provide evidence, not exhaustive proof of every possible execution.
