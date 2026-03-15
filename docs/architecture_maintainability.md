# Phase 32 - Maintainability and architecture review

## Scope and invariants

This change removes duplicated IPv4 numeric parsing in the bounded request gateway, CIDR parser, and policy matcher. The helper is **private** (`src/ipv4_parse_internal.h`), contains no global state, allocates no heap memory, and uses `inet_pton(AF_INET)` and `ntohl` to return one canonical host-order `uint32_t`. No exported symbols, public API signatures, rate-limiting decisions, synchronization primitives, or ABI promises change.

## Layer boundaries

```text
external caller
  -> request_policy  (immutable CIDR rules, local block/bypass)
     -> request_gateway (bounded ingress + response mapping)
        -> rate_service (clock, diagnostics, counters)
           -> ip_rate_limiter (IP table + bucket lookup)
              -> ip_atomic_table (atomic key publication)
              -> lockfree_bucket (atomic CAS admission)
                 -> bucket_state (pure transition model)

shared private utility: src/ipv4_parse_internal.h
  used by gateway + policy (not installed)
```

## Refactoring decision

The project previously used `inet_pton` and `ntohl` separately in three call sites. Keeping separate implementations risks inconsistent length checks, embedded-NUL handling, and host/network byte-order conversions. The new small header-only helper is `static inline` because only two source translation units need it and no library linkage change is warranted. This deliberately does **not** introduce a new public header or installed ABI symbol.

- `rl_ipv4_parse_span(ptr, len, out)` accepts 1..15 characters and rejects embedded NULs, an oversized length, null arguments, and invalid dotted IPv4 strings before changing `*out`.
- `rl_ipv4_parse_cstr(ptr, out)` expects a conventional valid NUL-terminated string and stops checking after the 16th character; it delegates to the span parser.
- CIDR prefix arithmetic, longest-prefix selection, block-over-bypass precedence, and token-bucket behavior are unchanged.
- `request_gateway_handle()` remains the legacy NUL-terminated API; `request_gateway_handle_n()` keeps its explicit-length contract.

## Ownership and concurrency review

1. Policies are built before worker threads and then treated as immutable.
2. Gateway and parser create only stack-local values; neither mutates global state.
3. Invalid ingress is rejected before clock reads, IP slot insertion, or counter updates.
4. The rate limiter must outlive all workers; destroy only after joining threads.
5. Atomic publication and CAS linearization remain in the existing modules; no new locks or atomics are introduced.

## Automated checks

```bash
make clean
make ipv4-internal-test
make request-gateway-test request-policy-test api-contract-test
make docs-test package-test
make static-check
make test
```

For Clang: `make clean && make CC=clang ipv4-internal-test request-gateway-test request-policy-test api-contract-test`.
The helper is intentionally under `src/`, therefore it is not included by the `include/*.h` installation wildcard. Check with `find build/stage -name '*ipv4*'` after a staged installation.

## Failure modes and boundaries

- Do not pass a pointer to an unreadable buffer or a length larger than the actual buffer; C cannot verify memory validity. The parser validates semantic bounds, not OS memory mapping.
- `rl_ipv4_parse_cstr` requires proper C-string lifetime and termination; bounded ingress uses the span API instead.
- Do not treat trusted-proxy or X-Forwarded-For claims as authenticated IPv4 input without a trust boundary.
- This code is still an in-process library, not an HTTP daemon.

## Review checklist

- [x] No public header or exported signature change.
- [x] Shared normalized IPv4 representation.
- [x] No mutations on parser failure.
- [x] CIDR and gateway parity checked via existing integration tests.
- [x] Source-only build/package workflow remains intact.
- [x] Study PDF maintained outside the repository.
