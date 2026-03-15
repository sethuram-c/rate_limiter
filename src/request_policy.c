#include "request_policy.h"

#include "ipv4_parse_internal.h"

#include <string.h>

static uint32_t prefix_mask(uint8_t prefix)
{
    if (prefix == 0U) {
        return 0U;
    }
    return UINT32_MAX << (32U - prefix);
}

/* Strictly accept ADDRESS/DECIMAL_PREFIX, nothing before or after. */
static bool parse_cidr(const char *cidr, request_policy_rule_t *out)
{
    const char *slash;
    uint32_t ip;
    size_t address_len;
    const char *p;
    unsigned int prefix = 0U;

    if (cidr == NULL || out == NULL) {
        return false;
    }
    slash = strchr(cidr, '/');
    if (slash == NULL) {
        return false;
    }
    address_len = (size_t)(slash - cidr);
    if (!rl_ipv4_parse_span(cidr, address_len, &ip) || slash[1] == '\0') {
        return false;
    }
    for (p = slash + 1; *p != '\0'; ++p) {
        if (*p < '0' || *p > '9') {
            return false;
        }
        prefix = prefix * 10U + (unsigned int)(*p - '0');
        if (prefix > 32U) { /* rejects large values before overflow */
            return false;
        }
    }
    out->prefix = (uint8_t)prefix;
    out->mask = prefix_mask(out->prefix);
    out->network = ip & out->mask;
    return true;
}

bool request_policy_add_rule(request_policy_t *policy, const char *cidr,
                             request_policy_action_t action)
{
    request_policy_rule_t rule;
    if (policy == NULL || policy->count >= REQUEST_POLICY_MAX_RULES ||
        (action != REQUEST_POLICY_BYPASS && action != REQUEST_POLICY_BLOCK) ||
        !parse_cidr(cidr, &rule)) {
        return false;
    }
    rule.action = action;
    policy->rules[policy->count] = rule;
    ++policy->count;
    return true;
}

bool request_policy_handle(const request_policy_t *policy,
                           rate_service_t *service,
                           const char *client_ipv4,
                           uint32_t requested_tokens,
                           request_policy_response_t *out)
{
    request_policy_response_t response = {0};
    request_gateway_response_t gateway;
    uint32_t ip;
    bool found = false;
    request_policy_rule_t best = {0};
    size_t i;

    if (policy == NULL || service == NULL || out == NULL ||
        policy->count > REQUEST_POLICY_MAX_RULES) {
        return false;
    }

    /* Invalid address/cost must not use exemptions to bypass validation. */
    if (requested_tokens > 0U &&
        rl_ipv4_parse_cstr(client_ipv4, &ip)) {
        for (i = 0U; i < policy->count; ++i) {
            const request_policy_rule_t *rule = &policy->rules[i];
            if ((ip & rule->mask) != rule->network) {
                continue;
            }
            if (!found || rule->prefix > best.prefix ||
                (rule->prefix == best.prefix &&
                 rule->action == REQUEST_POLICY_BLOCK &&
                 best.action == REQUEST_POLICY_BYPASS)) {
                best = *rule;
                found = true;
            }
        }
    }

    if (found) {
        response.rule_matched = true;
        response.matched_prefix = best.prefix;
        if (best.action == REQUEST_POLICY_BLOCK) {
            response.route = REQUEST_POLICY_BLOCKED;
            response.http_status = 403;
        } else {
            response.route = REQUEST_POLICY_BYPASSED;
            response.http_status = 200;
        }
    } else {
        /* Delegate bad IPv4/zero cost to the existing validation contract. */
        if (!request_gateway_handle(service, client_ipv4, requested_tokens,
                                    &gateway)) {
            return false;
        }
        response.route = REQUEST_POLICY_DELEGATED;
        response.http_status = gateway.http_status;
        response.forwarded = gateway.forwarded;
        response.has_service_result = gateway.forwarded;
        if (gateway.forwarded) {
            response.service_result = gateway.result;
        }
        response.has_remaining = gateway.has_remaining;
        response.remaining_tokens = gateway.remaining_tokens;
        response.has_retry_hint = gateway.has_retry_hint;
        response.retry_after_ms = gateway.retry_after_ms;
    }
    *out = response;
    return true;
}
