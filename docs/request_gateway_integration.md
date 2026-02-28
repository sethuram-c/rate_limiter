# Phase 16: HTTP-style request adapter and deterministic traffic simulation

This phase builds **an in-process adapter and trace simulator**, not a socket
listener, reverse proxy, or production HTTP server. The adapter illustrates
how an application converts client addresses and per-request costs into
`rate_service_check_detailed()` calls and turns the outcomes into response
metadata. It does not serialize HTTP or send data over a network.

## Request flow

```text
trusted client IPv4 text + token cost
                 |
        inet_pton(AF_INET)
          /             \
     invalid           valid
       |                 |
  local 400      ntohl(network IP)
  no clock read          |
  no slot claim   rate_service_check_detailed()
  no counter           |
              clock -> IP index -> atomic bucket CAS
                         |
                 decision-aligned state
                         |
         HTTP-like code / remaining / retry hint
```

The adapter is deliberately thin. All token balance updates, CAS loops,
clock error handling, and five service counters belong to the existing
rate-service layer. The adapter adds no new locks, allocation or clock reads;
`inet_pton()` itself is a userspace address parser and the caller's clock
callback can have its own synchronization/progress requirements.

## Public interface

`request_gateway_handle(service, client_ipv4, tokens, &response)` returns
false only when `response` is NULL. Otherwise, it initializes all fields and
reports whether an underlying service call was made (`forwarded`). Do not
confuse its Boolean function return with rate-limit admission: consult
`response.result` or `response.http_status`.

- `200 / ALLOWED`: request admitted, decision-aligned remaining balance.
- `429 / DENIED`: quota exhausted; retry estimate when representable.
- `400 / INVALID`: malformed IP rejected locally, or invalid token cost
  forwarded to the service when the textual IP was valid.
- `503 / TABLE_FULL`: service cannot allocate a new per-IP slot.
- `503 / CLOCK_ERROR`: time source failed; no token was consumed.

HTTP-like status codes are an **illustrative policy mapping**. They are not
sent on a socket. `429` and `503` should usually be treated differently by
clients. The caller should enforce input length bounds before passing untrusted
text to a C API, and should add authentication, proxy trust, and address
selection at a real ingress boundary.

The `remaining_tokens` value represents the committed bucket state from the
admission CAS, **not the instantaneous balance after other threads run**.
`retry_after_ms` is an earliest-eligibility hint, not an admission guarantee.
No hint is provided when the requested amount exceeds policy capacity.
For a real HTTP `Retry-After` field measured in whole seconds, a consumer
might conservatively calculate `ceil(retry_after_ms / 1000)` with checked
arithmetic; the adapter intentionally does not emit such a header.

### IPv4 parsing and endianness

`inet_pton(AF_INET, dotted_ip, &addr)` accepts valid dotted-decimal IPv4.
`addr.s_addr` contains network-order bits. `ntohl()` converts this to a stable
host-order numeric key expected by the hash table. All 32-bit IPv4 values,
including `0.0.0.0` and `255.255.255.255`, are valid keys in this system.
These are used solely as examples, **not** assertions that those addresses
should be permitted at an actual Internet boundary.

Do not use `sscanf("%u.%u.%u.%u")` without rigorous range and suffix checks;
partial matches can admit malformed strings. Do not treat arbitrary
`X-Forwarded-For` request headers as trusted client identity.

## Reproducible trace: capacity=2, refill one token per 100 ms, two IP slots

| Time ms | Client text | Cost | Code | Result | Balance | Retry ms |
|---:|---|---:|---:|---|---:|---:|
| 1000 | 192.0.2.1 | 1 | 200 | ALLOWED | 1 | - |
| 1000 | 192.0.2.1 | 1 | 200 | ALLOWED | 0 | - |
| 1000 | 192.0.2.1 | 1 | 429 | DENIED | 0 | 100 |
| 1000 | 198.51.100.7 | 2 | 200 | ALLOWED | 0 | - |
| 1050 | 192.0.2.1 | 1 | 429 | DENIED | 0 | 50 |
| 1100 | 192.0.2.1 | 1 | 200 | ALLOWED | 0 | - |
| 1100 | not-an-ip | 1 | 400 | INVALID (at ingress) | - | - |
| 1100 | 203.0.113.9 | 1 | 503 | TABLE_FULL | - | - |
| 1100 | 198.51.100.7 | 1 | 503 | CLOCK_ERROR | - | - |
| 1200 | 198.51.100.7 | 1 | 200 | ALLOWED | 1 | - |
| 1200 | 192.0.2.1 | 0 | 400 | INVALID (in service) | - | - |

Simulation clock is updated only when there are no concurrent workers. This
avoids data races in the example. A real-time gateway should use the existing
monotonic adapter; tests should use a synchronized or immutable clock.

**Accounting:** 11 simulated ingress events, 10 forwarded to service.
Final service counters: allowed=5, denied=2, table_full=1, invalid=1,
clock_error=1. The malformed IP is rejected before any service request, so
it does not increment the service invalid counter. Application-layer metrics
may track it independently if desired. Counter snapshots are exact after all
request work finishes, but five independent relaxed loads are not a
single transactional snapshot while threads are running.

## Concurrency regression test

Eight threads, 5,000 one-token calls each, across four different IP strings:
40,000 requests total. Each IP starts with 37 tokens. With an immutable 5,000
ms clock and 1,000 ms refill interval, no refill occurs. Exactly
`4 * 37 = 148` admissions are possible and 39,852 are denied. No socket,
network timing or sleeps make the result flaky. These tests verify observed
admissions but do not formally prove every possible interleaving.

## Linux exercise commands

```bash
make clean
make
make request-gateway-test
make request-gateway-demo
make test
make clean && make CC=clang request-gateway-test
make clean && make CC=gcc CFLAGS='-std=c11 -Wall -Wextra -Wpedantic -Werror -O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer' LDFLAGS='-fsanitize=address,undefined' request-gateway-test
```

Do not run `make clean` concurrently with another build. When changing
`CFLAGS`, clean first because Make cannot necessarily infer that flags
changed. Restore `make clean && make test` afterward.

## Lifecycle and non-goals

Initialize the rate service before accepting work. Workers may share it only
when its clock/context are concurrently safe. Stop and join workers before
`rate_service_destroy()`. No concurrent table resizing/deletion or dynamic
per-client policy changes are supported. The request-path CAS is lock-free on
supported platforms, but that does **not** make the entire in-process adapter
(including arbitrary clock callbacks and external integration) universally
lock-free. This phase is not a production HTTP service and adds no real
network I/O.

## Failure boundaries and interview prompts

1. Why must malformed IP addresses be rejected before key insertion?
2. Why do you call `ntohl()` after parsing dotted-quad IPv4 text?
3. Why should 429 and 503 be different even though both reject a request?
4. Why must a retry estimate not be described as guaranteed success?
5. Why is the four-IP admission total exactly 148 despite thread scheduling?
6. Why is it incorrect to claim that the final counters include invalid
   textual IPs that never reached `rate_service_check_detailed()`?
7. Why are fixed-cost trace tests more reproducible than sleeping between
   live requests?
8. How would a real reverse proxy authenticate forwarding headers?
