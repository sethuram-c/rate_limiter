# Phase 28 - Public API contracts and failure-path regression

## Why this phase exists

The original `request_gateway_handle` consumes a NUL-terminated IPv4 string.
That is appropriate for many C callers, but a network parser often supplies
`(pointer, length)` views into a request buffer. Treating such a view as a
C string without ensuring termination can read outside the intended slice.

Phase 28 adds `request_gateway_handle_n` with a bounded, explicit-length input
contract while preserving the original entrypoint. The new API also lets us
exercise error behavior end-to-end without depending on a real clock.

## Contract

```
bool request_gateway_handle_n(rate_service_t *service,
                              const char *client_ipv4,
                              size_t client_ipv4_len,
                              uint32_t requested_tokens,
                              request_gateway_response_t *out);
```

- `out == NULL`: return `false`, perform no request, do not touch input.
- For non-NULL `out`: return `true`, populate all fields, even on errors.
- `client_ipv4` is a byte array of exactly `client_ipv4_len` accessible bytes;
  the input need not contain a terminating NUL and is not modified.
- The buffer must remain readable and unmodified by other threads for the
  duration of the call; users must not race concurrent writes against it.
- Length must be between 1 and 15 inclusive for dotted-decimal IPv4.
- Embedded NUL, whitespace, invalid octets, incomplete and oversized inputs
  are rejected without touching clock, slot table, bucket or service counters.
- A bounded local array of 16 bytes receives the copy and terminator;
  `inet_pton(AF_INET, ...)` performs strict parsing.
- Valid input forwards to the existing detailed CAS-based rate-limiter API.
- The old `request_gateway_handle()` remains available and requires a proper
  NUL-terminated input string. It is implemented via the new entrypoint.

This library is still an in-process adapter, not an HTTP server. A real proxy
integration must authenticate client identity instead of blindly trusting
supplied forwarding headers.

## Important bounds reasoning

The maximum dotted-decimal IPv4 text is `255.255.255.255`: 15 bytes.
The copy only occurs after `length <= 15`. The local array is 16 bytes so
`text[length] = '\\0'` is always within bounds. `memchr` checks only the
advertised length, and an excessive length is rejected *before* dereference.
There is no dynamic allocation on this path.

The new API validates that the supplied pointer references enough accessible
bytes; C has no way to independently verify a dishonest length argument.
If the caller claims 15 accessible bytes but supplied only 3 bytes of storage,
its call violates the API precondition. Sanitizers may detect such misuse.

## Failure-path state accounting

1. Invalid input at ingress: `INVALID` / HTTP 400, `forwarded=false`, no
   clock read, no table claim, and no internal counter change.
2. Valid IP + zero-token request: forwarded and internally counted `INVALID`.
3. Clock callback failure: forwarded `CLOCK_ERROR` / HTTP 503, no new slot.
4. Full table: forwarded `TABLE_FULL` / HTTP 503; existing IP quota intact.
5. Empty quota: forwarded `DENIED` / HTTP 429; retry hint is only a lower-bound
   time estimate and must not remain set when a later error occurs.
6. Null output: no attempt, no side effect, return false.
7. Destroy only after all workers join; no concurrent destruction.

## Tests

The new suite in `tests/test_api_contracts.c` covers nonterminated byte
buffers, embedded NUL, oversized lengths, zero-length and partial views,
trailing whitespace, parity between legacy and length-based entrypoints,
retry-metadata clearing, clock and table failures, invalid cost, service
counters, and eight threads issuing 32,000 calls to two IPs. At fixed time
with 37 tokens for each IP, exactly 74 admissions are expected.

### Fedora quick start

```
make clean
make api-contract-test
make request-gateway-test
make ci-config-test
make static-check
make api-contract-asan
make test
```

`make test` includes `api-contract-test` and is used by both GCC and Clang
jobs in GitHub Actions. `api-contract-asan` is a focused local sanitizer run.
Static analysis and sanitizer success improve evidence but do not constitute
a formal proof of all concurrent executions or of bounds safety when callers
violate the pointer-and-length precondition.

## Phase 28 acceptance criteria

- Both old and new request APIs compile without changed call sites.
- The valid path uses exactly the original rate-service admission logic.
- Invalid ingress does not change the clock-read count, the slot count, or
  any of the five service counters.
- Failed service calls zero response metadata instead of leaking stale hints.
- Concurrent bounded requests do not overspend either bucket.
- GitHub Actions' three-job workflow still passes `make ci-config-test`.
- Training PDF remains outside Git.
