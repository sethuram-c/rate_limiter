#ifndef REQUEST_GATEWAY_H
#define REQUEST_GATEWAY_H

#include "rate_service.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Phase 16: an in-process HTTP-style adapter, NOT a socket/HTTP server.
 * Input is a NUL-terminated dotted-decimal IPv4 string, not a trusted
 * X-Forwarded-For header. Real network integrations must obtain the client
 * address from a trusted transport/proxy boundary and authenticate headers.
 *
 * No allocation or locks are introduced by this adapter. The injected
 * clock callback used by rate_service may have its own progress properties.
 * Concurrent calls are safe if service lifetime and its clock are safe.
 */
typedef struct {
    rate_service_result_t result;
    rate_service_status_t policy_status;
    int http_status;                  /* illustrative HTTP mapping, not a response writer */
    bool forwarded;                   /* was rate_service_check_detailed called? */
    bool has_remaining;
    uint16_t remaining_tokens;        /* decision-aligned; not a live reading */
    bool has_retry_hint;
    uint64_t retry_after_ms;          /* earliest possible retry; not admission guarantee */
} request_gateway_response_t;

/* False ONLY when out == NULL: no underlying request is performed.
 * On true, fills every field. Malformed textual IPs are rejected at ingress
 * without consuming tokens, claiming a slot, reading the clock, or incrementing
 * the rate_service counters. A zero-token request with a valid IP IS forwarded,
 * and will be counted as INVALID by an initialized service.
 * IPv4 addresses are normalized with inet_pton(AF_INET) + ntohl(), so
 * distinct textual addresses cannot alias due to ad hoc parsing.
 */
bool request_gateway_handle(rate_service_t *service,
                            const char *client_ipv4,
                            uint32_t requested_tokens,
                            request_gateway_response_t *out);

/* Phase 28: bounded, length-aware ingress for buffers that are NOT required
 * to be NUL terminated. Accepts only 1..15 bytes representing a complete
 * dotted-decimal IPv4 address. Embedded NUL bytes and trailing bytes are
 * rejected, even if inet_pton() would parse a prefix. This entrypoint never
 * reads beyond client_ipv4_len, and allocates no memory.
 *
 * On malformed/null input, returns true with a non-forwarded HTTP 400
 * response; neither clock, token state, nor service counters are touched.
 * On out == NULL, returns false and does not inspect the input or attempt a
 * request. The input buffer must not be modified concurrently.
 * Existing request_gateway_handle() remains compatible and requires a
 * conventional NUL-terminated string.
 */
bool request_gateway_handle_n(rate_service_t *service,
                              const char *client_ipv4,
                              size_t client_ipv4_len,
                              uint32_t requested_tokens,
                              request_gateway_response_t *out);

#endif
