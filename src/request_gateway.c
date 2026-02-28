#include "request_gateway.h"

#include <arpa/inet.h>
#include <stddef.h>

static int to_http_status(rate_service_result_t result)
{
    switch (result) {
    case RATE_SERVICE_ALLOWED: return 200;
    case RATE_SERVICE_DENIED: return 429;
    case RATE_SERVICE_TABLE_FULL: return 503;
    case RATE_SERVICE_INVALID: return 400;
    case RATE_SERVICE_CLOCK_ERROR: return 503;
    }
    return 503;
}

bool request_gateway_handle(rate_service_t *service,
                            const char *client_ipv4,
                            uint32_t requested_tokens,
                            request_gateway_response_t *out)
{
    request_gateway_response_t response = {0};
    struct in_addr parsed;
    rate_service_diagnostic_t diagnostic;

    if (out == NULL) {
        return false;
    }
    response.result = RATE_SERVICE_INVALID;
    if (service != NULL && client_ipv4 != NULL &&
        inet_pton(AF_INET, client_ipv4, &parsed) == 1) {
        const uint32_t ip = ntohl(parsed.s_addr);
        /* A detailed check always returns true for non-NULL output. */
        (void)rate_service_check_detailed(service, ip,
                                          requested_tokens, &diagnostic);
        response.forwarded = true;
        response.result = diagnostic.result;
        if (diagnostic.has_bucket_state) {
            response.has_remaining = true;
            response.remaining_tokens = diagnostic.remaining_tokens;
        }
        if (diagnostic.result == RATE_SERVICE_DENIED &&
            diagnostic.retry_possible) {
            response.has_retry_hint = true;
            response.retry_after_ms = diagnostic.retry_after_ms;
        }
    }
    response.policy_status = rate_service_classify(response.result);
    response.http_status = to_http_status(response.result);
    *out = response;
    return true;
}
