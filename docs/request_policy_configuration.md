# Phase 17 - Static IPv4 CIDR policy at the request gateway

The Phase 17 module adds **immutable application-side policy** on top of the
existing in-process request adapter. It is not an HTTP server, firewall, or a
substitute for network-layer authentication. The Phase 16 `request_gateway_handle`
interface and underlying CAS bucket are unchanged.

## Public API and lifetime

`include/request_policy.h` provides `request_policy_t`,
`request_policy_add_rule()`, and `request_policy_handle()`.

1. Zero-initialize a `request_policy_t`.
2. Before launching workers, call `request_policy_add_rule` up to 32 times.
3. Initialize `rate_service_t` normally, including a safe clock provider.
4. Share both objects *read-only* across concurrent workers.
5. Join the workers before destroying the service or editing the policy.

The routing logic allocates no memory and holds no mutexes. This does not turn
an arbitrary clock callback into a lock-free function; clock providers can
block, so the lock-free progress property applies to the atomic data structures
and not automatically to every application request.

## CIDR parsing and precedence

CIDR syntax is `A.B.C.D/P`, where `A.B.C.D` is an IPv4 dotted-decimal literal
and `P` is an integer in `[0,32]`. Rules normalize non-canonical addresses:
`192.0.2.37/24` means `192.0.2.0/24`. Inputs such as `10.0.0.1/33`,
`10.0.0.1/-1` or `10.0.0.1/24x` are rejected without changing the rule set.

Address and network are converted from network byte order with `ntohl`.
For a prefix length `p`, the 32-bit mask is `0` if `p==0` and otherwise
`UINT32_MAX << (32-p)`. The special case prevents undefined shift by 32.
An IP matches when `(ip & rule.mask) == rule.network`.

**Longest matching prefix wins**. For rules with the same prefix length,
`BLOCK` wins over `BYPASS`, regardless of order. Thus a broad exemption
`192.0.2.0/24` can coexist with a stricter block `192.0.2.42/32`.
A `0.0.0.0/0` rule matches all IPv4 addresses; use this with caution.

## Outcomes and accounting

| Route | HTTP-style status | Forwarded? | Service tokens/counters |
|---|---:|---|---|
| `REQUEST_POLICY_BYPASSED` | 200 | no | unchanged |
| `REQUEST_POLICY_BLOCKED` | 403 | no | unchanged |
| `REQUEST_POLICY_DELEGATED` | 200/400/429/503 | depends on input | same as Phase 16 |

Bad textual IPv4 input delegates to Phase 16 ingress validation and is rejected
before it reaches the service. A zero-cost request delegates to the service and
is counted as `INVALID` when the service is initialized. Local policy outcomes
carry **no** service result, token balance or retry hint. A 403 policy block
must not be reported as a 429 quota denial.

A static bypass is a deliberate security decision: it also skips per-IP limits
for requests larger than normal bucket capacity. Apply exemptions only to
trusted principals, and *never* trust client-supplied `X-Forwarded-For`
without explicit trusted-proxy handling.

## Verification

```sh
make request-policy-test
make request-policy-demo
make test
CC=clang make clean test
```

In the eight-thread stress test there are 40,000 total requests at a fixed
clock instant: 10,000 bypassed, 10,000 blocked, 10,000 malformed at ingress,
and 10,000 sent to one 41-token bucket. Exactly 41 of those 10,000 requests
are admitted and 9,959 are throttled. Only the forwarded 10,000 appear in
`rate_service_get_stats`; no rejected or bypassed request claims a slot.

## Trade-offs, interview discussion, and non-goals

- Every policy request scans at most 32 rules: bounded O(R) time, O(1) storage.
  Larger policies should use precomputed tries or longest-prefix structures.
- The rules are safe to share during reads because they are never mutated while
  workers are running. Mutating during reads is a C data race even though the
  underlying bucket uses atomics.
- This is fixed-capacity static configuration. No dynamic reload, ACL
  persistence, authentication, IPv6, parsing HTTP sockets or hot updates.
- `request_policy_handle` returns false for a NULL policy, service or output;
  the supplied response object stays unchanged in that case.
- For real production use, IP-level exemptions must be part of an explicit
  threat model. Many users share NATs and a single user can rotate addresses.
