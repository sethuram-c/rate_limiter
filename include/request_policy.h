#ifndef REQUEST_POLICY_H
#define REQUEST_POLICY_H

#include "request_gateway.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Phase 17: immutable, allocation-free IPv4 CIDR ingress policy.
 * Configure fully before starting workers; NEVER modify during requests.
 * Client IP must come from a trusted network/proxy boundary.
 * This is an in-process adapter, not a network firewall or HTTP server.
 */
#define REQUEST_POLICY_MAX_RULES 32U

typedef enum {
    REQUEST_POLICY_BYPASS, /* accept without spending tokens */
    REQUEST_POLICY_BLOCK   /* HTTP-style 403, independent of quota */
} request_policy_action_t;

typedef struct {
    uint32_t network; /* canonical host-order network address */
    uint32_t mask;    /* host-order mask */
    uint8_t prefix;   /* 0..32 */
    request_policy_action_t action;
} request_policy_rule_t;

typedef struct {
    size_t count;
    request_policy_rule_t rules[REQUEST_POLICY_MAX_RULES];
} request_policy_t;

typedef enum {
    REQUEST_POLICY_DELEGATED, /* underlying gateway supplied status */
    REQUEST_POLICY_BYPASSED,  /* local 200, no service call */
    REQUEST_POLICY_BLOCKED    /* local 403, no service call */
} request_policy_route_t;

typedef struct {
    request_policy_route_t route;
    int http_status;
    bool forwarded;           /* did a service request occur? */
    bool rule_matched;
    uint8_t matched_prefix;  /* valid only when rule_matched is true */
    bool has_service_result; /* service_result meaningful only when forwarded */
    rate_service_result_t service_result;
    bool has_remaining;
    uint16_t remaining_tokens;
    bool has_retry_hint;
    uint64_t retry_after_ms;
} request_policy_response_t;

/* Zero-initialize request_policy_t before adding rules. Only call add_rule
 * before worker threads start. Invalid/full additions leave state unchanged.
 * CIDR must be dotted IPv4 followed by /0..32. Host bits are normalized.
 * Longest matching prefix wins; equal-length ties choose BLOCK over BYPASS.
 */
bool request_policy_add_rule(request_policy_t *policy, const char *cidr,
                             request_policy_action_t action);

/* Returns false without touching *out for NULL policy/service/out.
 * Malformed IPv4 and zero-token requests delegate to the original gateway
 * validation path (HTTP-style 400). Local decisions do not consume tokens,
 * read the clock, claim slots or increment rate_service counters.
 * HTTP 403 represents a policy block, not token-bucket throttling (429).
 */
bool request_policy_handle(const request_policy_t *policy,
                           rate_service_t *service,
                           const char *client_ipv4,
                           uint32_t requested_tokens,
                           request_policy_response_t *out);

#endif
