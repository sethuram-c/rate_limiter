# Architecture Decision Records (ADRs) | Phase 36

Decisions below describe the **implemented** design. They are documentation,
not a claim that the public ABI is frozen.

## ADR-001: Fixed-capacity, insert-only IPv4 index

**Status:** Accepted. **Context:** Concurrent lookup/insertion should avoid
request-path allocation and complex memory reclamation. **Decision:** Use
power-of-two open-addressing with tagged 64-bit atomic slots; every valid
32-bit IPv4 value remains representable. Key publication uses atomic CAS,
with preinitialized buckets indexed by slot. **Trade-off:** A bounded table
can become full; keys cannot be evicted, entries cannot be resized or safely
deleted during concurrent use. Alternatives: mutexed map, lock-free dynamic
map with reclamation.

## ADR-002: Pack token balance and refill anchor in one 64-bit atomic

**Status:** Accepted. **Context:** Separate atomics could allow inconsistent
combinations (double refills or lost elapsed time). **Decision:** Pack 48-bit
monotonic millisecond anchor and 16-bit available tokens into one state and
commit a complete transition by CAS. **Trade-off:** Limited time/token ranges;
CAS contention can hurt throughput. A denied request can still commit a new
refill anchor. Alternatives: mutexed state; wider atomic types with weaker
portability. The existing mutex implementation provides a comparison baseline.

## ADR-003: Monotonic and injected time sources

**Status:** Accepted. **Decision:** The sequential model takes injected time;
`rate_service` can use an injected callback or Linux `CLOCK_MONOTONIC`.
Sample the service clock before any new-IP insertion. **Consequences:**
Reproducible tests and clock-failure isolation. External callbacks can have
their own scheduling or blocking behavior; the whole service API should not
be marketed as lock-free solely because the bucket update is lock-free.

## ADR-004: Immutable ingress policies and no network listener

**Status:** Accepted. **Decision:** CIDR BYPASS/BLOCK rules are created before
worker traffic, then read without mutation. Parsing, policy and HTTP-style
mapping are **in-process** adapters. The caller owns trusted client identity.
**Consequences:** No live policy updates, no sockets, no authenticated proxy
integration, and no actual HTTP serialization. Such behavior must be supplied
by a hosting application.

## ADR-005: Quiescent teardown and no concurrent reclamation

**Status:** Accepted. **Decision:** Allocate and initialize both arrays
before starting readers; join all readers before freeing them. The table is
insert-only and the worker hot path makes no allocations. **Consequences:**
Simple slot ownership, at the cost of requiring explicit application lifetime
coordination. Concurrent `destroy()` remains invalid, not internally solved.

## ADR-006: Opt-in 64-byte bucket layout

**Status:** Experimental / optional. **Decision:** Retain compact layout by
default; `RL_CACHELINE_PADDED` provides 64-byte-aligned bucket slots for
A/B experiments. **Consequences:** Larger memory footprint and mixed measured
performance. It is not a guaranteed optimization; compile all translation
units using `lf_bucket_t` with consistent layout flags.

## Review checklist

- Can you identify the separate IP-key insertion and token decision CAS points?
- Which request outcomes do *not* claim a new slot?
- Why are all bucket payloads initialized before worker publication?
- What changes when callbacks, allocators, or teardown enter the picture?
- Where does the service count a request; which ingress outcomes bypass it?
- Can a denial commit a refill? Why must failed CAS recompute the plan?
- Why should throughput and p99 results be reported per workload, not as a
  universal `lock-free is faster` claim?

The source-grounded diagrams are in [system_architecture.md](system_architecture.md).
Personal learning PDFs remain outside Git.
