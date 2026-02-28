#include "request_policy.h"

#include <arpa/inet.h>
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
    char address[16];
    struct in_addr parsed;
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
    if (address_len == 0U || address_len >= sizeof(address)) {
        return false;
    }
    memcpy(address, cidr, address_len);
    address[address_len] = '\0';
    if (inet_pton(AF_INET, address, &parsed) != 1 || slash[1] == '\0') {
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
    out->network = ntohl(parsed.s_addr) & out->mask;
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
    struct in_addr parsed;
    bool found = false;
    request_policy_rule_t best = {0};
    size_t i;

    if (policy == NULL || service == NULL || out == NULL ||
        policy->count > REQUEST_POLICY_MAX_RULES) {
        return false;
    }

    /* Invalid address/cost must not use exemptions to bypass validation. */
    if (requested_tokens > 0U && client_ipv4 != NULL &&
        inet_pton(AF_INET, client_ipv4, &parsed) == 1) {
        uint32_t ip = ntohl(parsed.s_addr);
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
