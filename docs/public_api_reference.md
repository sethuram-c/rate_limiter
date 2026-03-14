# Public API reference — experimental v0.1.0

**Audience:** C11 application developers using the in-process Linux/POSIX rate limiter.
**Canonical installed header:** `#include <ratelimiter/ratelimiter.h>`.
**Source of truth:** declarations and comments in `include/rate_service.h`, `include/rate_config.h`, `include/request_gateway.h`, `include/request_policy.h`, and `include/rate_version.h`.
**Build artifact:** `libratelimiter.a`. This is a *development* interface, not a stable binary ABI and not a network server.

## 1. Selecting an entry point

| Task | API | Key consideration |
|---|---|---|
| Read version | `RL_VERSION_STRING`, `rl_version_string()` | compare installed header and archive; equality cannot prove ABI compatibility |
| Validate configuration without side effects | `rate_config_plan()` | returns reason and required payload allocation sizes |
| Initialize with limits | `rate_service_init_with_config()` | requires zero-initialized service; may fail; call before workers |
| Initialize traditional bucket policy | `rate_service_init()` | does **not** enforce a memory budget |
| Use the OS monotonic clock | `rate_service_init_monotonic()` | no custom clock callback required |
| Check normalized IPv4 address | `rate_service_check()` | host-order `uint32_t`; the caller owns IP parsing |
| Get CAS-aligned decision metadata | `rate_service_check_detailed()` | retry estimate is not a promise |
| Process dotted-decimal IPv4 | `request_gateway_handle()` | requires a NUL-terminated string |
| Process length-bounded IPv4 | `request_gateway_handle_n()` | explicit length, no terminator required |
| Apply CIDR bypass/block policies | `request_policy_add_rule()`, `request_policy_handle()` | policy fully configured before workers |
| Inspect counters | `rate_service_get_stats()` | coherent aggregate only after joining workers |
| Stop and reclaim resources | `rate_service_destroy()` | **only after all threads have stopped/joined** |

## 2. Version contract

- `RL_VERSION_MAJOR`, `RL_VERSION_MINOR`, `RL_VERSION_PATCH`, `RL_VERSION_STRING` expose **compile-time** version metadata (`0.1.0`).
- `RL_VERSION_AT_LEAST(major,minor,patch)` is a preprocessor-friendly version comparison.
- `rl_version_string()` returns a constant, static-lifetime pointer from the **linked library**. Never free or modify it.
- Prefer building the consumer and library with the same configuration, compiler ABI and headers. Public structs (including atomics) expose layout. **Stable ABI is not promised.**

## 3. Configuration and lifecycle

```c
#include <ratelimiter/ratelimiter.h>

rate_service_t service = {0};
const rate_config_t config = {
    .slots = 8U,
    .capacity_tokens = 2U,
    .refill_interval_ms = 100U,
    .memory_budget_bytes = 4096U
};
rate_config_plan_t plan;
rate_config_status_t status = rate_config_plan(&config, &plan);
if (status != RATE_CONFIG_OK) {
    /* rate_config_status_string(status) provides diagnostics. */
}
/* After a successful plan, initialize before creating request threads. */
```

The slot count must be a positive power of two; capacity is 1..`UINT16_MAX`; the refill interval must fit the packed timestamp model; the memory budget must cover both array payloads. `plan.total_array_bytes` is **not** RSS and does **not** include malloc metadata, stack memory or the service object itself.

Call `rate_service_init_with_config(&service, &config, clock_fn, context)` once while quiescent. A clock callback must supply milliseconds from one consistent, monotonic epoch, and must be thread-safe if called from multiple workers. For a live Linux application, `rate_service_init_monotonic(&service, slots, &policy)` uses `CLOCK_MONOTONIC`, but does not apply the `rate_config_t` memory budget.

Once initialized, do not mutate `service`, `clock_context`, `policy`, or the referenced clock state unsafely. The library has a **fixed-capacity** hash index; no eviction, deletion, resize, or persistence. A newly encountered IP cannot enter a full table; existing entries continue to work.

### Destruction rule

1. Stop admitting new work in the application.
2. Ask workers to finish; `pthread_join` every worker that may still use the service or policy.
3. Call `rate_service_get_stats()` for the exact final counter snapshot (optional).
4. Call `rate_service_destroy(&service)`.

Destroying a live service or changing policies while readers are active is outside the contract. `rate_service_destroy` is safe for an empty/failed object but is not a replacement for coordinating thread shutdown.

## 4. Core request results

`rate_service_check(service, host_order_ipv4, tokens)` returns one of:

| Enum | Meaning | Typical treatment |
|---|---|---|
| `RATE_SERVICE_ALLOWED` | quota consumed successfully | continue processing |
| `RATE_SERVICE_DENIED` | valid request lacks tokens | throttle; consider retry hint |
| `RATE_SERVICE_TABLE_FULL` | new IP cannot be stored | capacity or availability response |
| `RATE_SERVICE_INVALID` | invalid amount, service or timestamp state | reject / log caller error |
| `RATE_SERVICE_CLOCK_ERROR` | clock sampling failed | temporary service failure |

A *denial* is not an internal error. Invalid tokens (for example zero) must not claim a new slot. Clock failure is checked before insertion. The raw `uint32_t` IPv4 argument must already be in the internal **host-order normalized** representation: external code normally should use the gateway API instead.

`rate_service_check_detailed()` returns `false` only if its `out` pointer is `NULL`; otherwise it returns `true` and sets `out->result`. Its `remaining_tokens` and refill metadata describe the same successful CAS as that decision. They are **historical decision metadata**, not a second stable read of the bucket. A nonzero retry hint is the earliest estimated eligibility and cannot guarantee a future admission under concurrent contention.

## 5. HTTP-style gateway (no sockets)

The adapter maps **in-process** request decisions to illustrative HTTP status numbers:

| Status | Meaning in adapter | Does it call underlying service? |
|---|---|---|
| 200 | allowed | yes, unless policy bypass |
| 400 | malformed input or invalid request | malformed textual IP: no; valid IPv4 with invalid cost: yes |
| 403 | blocked CIDR policy | no |
| 429 | token-bucket denial | yes |
| 503 | table full or underlying service unavailable | usually yes |

`request_gateway_handle()` requires conventional NUL-terminated input. For a data buffer without a terminator, call:

```c
request_gateway_response_t response;
const char address[] = {'1','9','2','.','0','.','2','.','1','0'};
bool called = request_gateway_handle_n(
    &service, address, sizeof address, 1U, &response);
```

The bounded API accepts complete dotted IPv4 strings of **1..15 bytes**. Embedded NULs, trailing junk and overlong buffers are rejected. If `out == NULL`, it returns `false` without attempting a request. A malformed buffer returns `true` with `http_status == 400` and `forwarded == false`; it does **not** sample the clock, claim an IP slot, consume tokens or increment service counters.

Neither gateway API reads sockets, constructs HTTP responses, parses TLS, or validates `X-Forwarded-For`. **Trust client IP addresses only after authenticating the transport/proxy boundary.**

## 6. Immutable CIDR routing rules

- Start with `request_policy_t policy = {0}`.
- Call `request_policy_add_rule(&policy, "203.0.113.0/24", REQUEST_POLICY_BYPASS)` and/or `REQUEST_POLICY_BLOCK` **before** worker requests begin.
- `request_policy_handle(&policy, &service, text_ip, tokens, &response)` chooses the most specific CIDR match. On equal prefix length, BLOCK wins over BYPASS.
- Rules are static (at most `REQUEST_POLICY_MAX_RULES`, currently 32). They are **not** a network firewall.
- A matched BYPASS responds with illustrative 200; BLOCK with illustrative 403. Neither path forwards to the rate service, uses a bucket, or increases the rate-service counters.
- Unmatched, malformed or zero-token input follows the gateway behavior. A `false` return with null policy/service/out does not alter `out`.

## 7. Status counters and consistency

`rate_service_get_stats(&service, &snapshot)` copies five independent atomic counters: `allowed`, `denied`, `table_full`, `invalid`, `clock_error`. They count requests **that reach the rate service**, not all traffic at an ingress boundary. A malformed textual address, CIDR bypass and CIDR block do not affect them.

Each counter is thread-safe, but the five-field snapshot during traffic is **not transactional**. For an exact accounting invariant, join all workers and then read the counters. Values may wrap modulo `2^64` after extremely many requests.

## 8. Threading, progress and unsupported features

- The packed bucket uses an atomic 64-bit CAS operation with platform lock-free capability checks. Its progress guarantee does not automatically extend to user clock callbacks, system calls or application behavior.
- The IP index is fixed-capacity, key publication is atomic, and bucket storage is initialized before keys can be published. No concurrent eviction or reclamation.
- A particular request is **not wait-free**: CAS contention and probe retries can delay an individual thread.
- No automatic distributed limits, IPC/shared-memory quotas, HTTP listener, live table resizing, persistence, or proxy-header authentication.
- Changing the `RL_CACHELINE_PADDED` build setting between binary and consumer builds is unsupported because it changes public struct layout.

## 9. Public header map and further reading

- `include/ratelimiter.h` — umbrella include; use this first.
- `include/rate_service.h` — initialized service, clock callback, decisions, diagnostics, counters, lifecycle.
- `include/rate_config.h` — memory-budget validation and type declarations.
- `include/request_gateway.h` — checked IPv4 textual entry points.
- `include/request_policy.h` — immutable CIDR decision layer.
- `include/rate_version.h` — source/runtime version handshake.
- [Developer integration guide](developer_integration_guide.md) — exact install and compile walkthrough.
- [Library packaging](library_packaging.md) — archive layout and transitive header installation.
- [Release readiness](release_readiness.md) — scope of pre-1.0 version and release checks.

**Contract authority:** This page explains API declarations but does not supersede the headers; for changed versions, read the installed headers and rerun `make docs-test`.
