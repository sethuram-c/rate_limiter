# Phase 9 - Fixed-capacity per-IP hash table design

This document is source-tree architecture documentation. The detailed learning
PDF is a separate personal reference, **not** part of the Git repository.

## 1. Scope and API boundary

This phase creates a **sequential reference model**, not a thread-safe table.
`ip_table_plan_probe` inspects an array of `ip_model_slot_t`; a slot contains
an explicit `occupied` bit and a 32-bit IPv4 key. Every 32-bit key is legal:
`0x00000000` and `0xffffffff` cannot be reserved as empty sentinels.

All capacities are nonzero powers of two. This enables index masking:

```c
start = ip_table_plan_hash(ip) & (capacity - 1);
index = (start + step) & (capacity - 1);
```

The bounded `step` values are 0 to `capacity-1`. No removal or resize is
supported, so the first empty slot terminates a lookup safely *in this
sequential model*. A probe scans at most `capacity` slots.

## 2. Probe invariants

1. A key never changes after insertion.
2. Every occupied slot on a key's search path before the key is either a
   collision or an earlier insertion; search must not stop prematurely.
3. A missing key returns the first empty slot on its probe path, if any.
4. Duplicate insertion finds the previously inserted key (idempotence).
5. When full, a new distinct key returns FULL without overwriting any slot.
6. No deletion/tombstone is supported, so deletion cannot break lookup.

## 3. Example: wraparound

Imagine a capacity-eight table and four different IP keys all hashing to
index seven. They occupy indices 7, 0, 1, 2 in insertion order. A duplicate
of the second key probes 7 then 0 and returns FOUND; it must not create a
second entry.

## 4. Why publication is harder than probing

The concurrent table eventually needs to associate each key with a completely
initialized `lf_bucket_t`. Publishing the key first, then writing ordinary
bucket fields, allows a racing reader to see an uninitialized bucket: this is
an invalid design even if the key itself is atomic. A simple RESERVED marker
can also cause progress problems if the reserving thread is delayed.

One possible future design is to allocate and fully initialize an immutable
entry **before** placing its pointer into an atomic table slot with a release
compare-exchange. Readers use acquire loads before touching its initialized
fields. Competing same-key insertions must converge to exactly one entry.
All published objects must outlive active readers; reclamation is deferred
until a quiescent shutdown because concurrent deletion is outside scope.

**Progress caveat:** `malloc` and `free` may block. A table that allocates on
its insertion path cannot claim that complete insertion is lock-free just
because its slot-claim CAS is lock-free. Future designs must distinguish
lock-free lookup/request processing from allocation and first-time insertion.

An alternative preinitialized slot design also needs a careful answer for the
bucket's *first request timestamp*; initializing every bucket at table startup
can give newly seen IPs an incorrect refill baseline. Phase 10 will evaluate
these trade-offs and state actual guarantees, not assume them.

## 5. Sizing and load factor

At high occupancy, linear probing forms clusters and collisions get expensive.
Prefer provisioning well above the expected number of unique active clients
(e.g., targeting around 50-70% occupancy initially), while measuring real
probe counts under representative workloads. Fixed capacity means no rehash
and no deletion. On exhaustion, the API must report a distinct table-full
error; silent fail-open or fail-closed behavior must be an application choice.

## 6. Hash model and trust boundary

`ip_table_plan_hash` uses a deterministic 32-bit avalanche finalizer to make
common integer patterns less clustered. It is **not cryptographic**, not
seeded, and provides no deliberate collision-attack resistance. For an
Internet-facing service where clients can select IPs or keys adversarially,
consider a secret keyed hash and relevant threat-model review in later phases.

IPv4 canonicalization is an integration concern: a caller should convert
human-readable dotted addresses to a canonical 32-bit representation before
calling this model. Be explicit about host-vs-network byte order when mapping
`struct in_addr` to `uint32_t`.

## 7. Verification checklist

```sh
make clean
make
make ip-plan-test
make ip-plan-demo
make test
make clean && make CC=clang && make CC=clang test
```

The Phase 9 test suite includes 60,000 deterministic operations compared with
an independent linear-search set, forced collisions and wraparound, duplicate
lookups, zero and UINT32_MAX IP keys, one-slot capacity, full-table behavior,
and unchanged outputs on bad input. These tests verify this sequential model;
they do **not** establish concurrent safety or lock-free progress.
