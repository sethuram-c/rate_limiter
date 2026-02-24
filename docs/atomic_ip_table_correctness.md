# Phase 10: concurrent IPv4 key index - correctness and progress

This document belongs in the **source repository**. The extensive Phase 10
learning PDF is a **separate personal document**, outside Git.

## Current scope

This is a fixed-capacity, insertion-only, lock-free **key index**, not a complete
per-IP rate limiter. It maps canonical `uint32_t` IPv4 keys to stable slot
indices. The standalone lock-free token bucket from Phase 7 is unchanged.
Phase 11 will associate a safely initialized bucket with each published key.
**Never initialize non-atomic bucket fields after publishing a key.**

## Representation

Each slot contains one `_Atomic uint64_t`. Zero means empty. Any occupied slot
contains `(UINT64_C(1) << 32) | ip`. The high marker makes all `uint32_t`
values legal, including 0 and UINT32_MAX. The slot is written once using a
strong CAS, and remains unchanged until quiescent destruction.

The table allocates and atomically initializes its entire slot array at startup;
no allocation or freeing occurs on concurrent lookup/insertion paths. It checks
`atomic_is_lock_free` and refuses to initialize on unsupported targets. Table
lifetime is externally synchronized: stop/join readers and writers **before**
destroying or reinitializing.

## Probe algorithm and invariants

1. `home = hash(ip) & (capacity - 1)`, as in the Phase 9 reference model.
2. Sequentially examine `home, home+1, ...` modulo power-of-two capacity.
3. If a slot equals the tagged key, report EXISTING or FOUND.
4. If empty, attempt strong CAS from EMPTY to the tagged key. Successful CAS is
   the unique INSERTED event. If CAS fails, examine the winning key and proceed.
5. If every slot is occupied by other keys, report FULL.
6. No key is deleted, moved, or overwritten; once an entry is published, it
   remains reachable along the same probe chain.

There can be no **permanently empty hole** before a published key: inserting at
slot k requires observing all prior probe positions occupied. An insertion that
loses a race must observe the winner before advancing. Two threads racing to
insert the same key cannot both win a CAS at distinct slots: any loser either
observes the winner at its current slot or encounters it on its probe path.

## Memory ordering and linearization

Phase 10 deliberately uses `memory_order_seq_cst` for the entire index. This
provides a single global order for atomic slot loads and CAS operations, which
simplifies reasoning about cross-slot linear probing. We do **not** prematurely
optimize to relaxed operations. Successful insertion linearizes at the winning
CAS; a duplicate insertion linearizes at the load or failed CAS that observes
its key; a negative lookup linearizes at the first empty slot observed (or a
full-probe observation for a full table).

The tagging CAS also publishes the key as a single atomic value, so readers
never see a partially written IP. Phase 11 will have to ensure any associated
bucket is initialized **before** key publication. An acquire/release protocol
may be evaluated after correctness is established, but optimizing cross-slot
ordering requires a new proof and tests.

## Progress and boundedness

Every lookup or insertion performs at most `capacity` probes. Strong CAS does
not permit spurious failure; each slot changes from zero to occupied at most
once. No locks, sleeps, malloc, or free occur in those operations. They depend
only on lock-free 64-bit atomics on a supported target. This is a *lock-free
key index* in the stated operational model, not a claim that table setup,
shutdown or future high-level request handling is lock-free. Worst-case probe
cost is O(capacity) and load factor influences typical cost.

## Contracts and limitations

- Only canonical `uint32_t` IPv4 values are supported; IPv6 is out of scope.
- Caller provides a nonzero power-of-two entry count and zero-initialized
  `ip_atomic_table_t` prior to initialization.
- Fixed capacity, no resizing, deletion, tombstones, aging, or eviction.
- FULL is explicit; the later application must select a failure policy.
- Hashing is deterministic and **not cryptographically keyed**; vulnerable to
  adversarial collision patterns if clients control their keys.
- The `count_quiescent` helper is only meaningful after threads are joined.
- No attached bucket payload or real network adapter exists in Phase 10.

## Verification

```bash
make clean && make && make test
make ip-atomic-test && make ip-atomic-demo
make clean && make CC=clang && make CC=clang test
```

New tests: 204,800 concurrent insert-or-find operations by eight threads over
128 shared keys (exactly 128 INSERTED); 24 simultaneously competing unique
keys for a 16-slot table (16 INSERTED, 8 FULL); collisions, duplicate reuse,
probe lookup, maximum key values, one-slot table, invalid inputs and destroy.
This gives executable evidence, **not an exhaustive formal proof**.
