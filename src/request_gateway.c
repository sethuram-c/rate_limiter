#include "request_gateway.h"

#include <arpa/inet.h>
#include <stddef.h>
#include <string.h>

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

/* A bounded copy is important: inet_pton() accepts NUL-terminated strings,
 * whereas the new public API accepts a buffer and an explicit length. */
bool request_gateway_handle_n(rate_service_t *service,
                              const char *client_ipv4,
                              size_t client_ipv4_len,
                              uint32_t requested_tokens,
                              request_gateway_response_t *out)
{
    enum { IPV4_MAX_CHARS = 15 };
    request_gateway_response_t response = {0};
    rate_service_diagnostic_t diagnostic;
    struct in_addr parsed;
    char text[IPV4_MAX_CHARS + 1U];

    if (out == NULL) {
        return false;
    }
    response.result = RATE_SERVICE_INVALID;
    if (service != NULL && client_ipv4 != NULL &&
        client_ipv4_len > 0U && client_ipv4_len <= IPV4_MAX_CHARS &&
        memchr(client_ipv4, '\0', client_ipv4_len) == NULL) {
        memcpy(text, client_ipv4, client_ipv4_len);
        text[client_ipv4_len] = '\0';
        if (inet_pton(AF_INET, text, &parsed) == 1) {
            const uint32_t ip = ntohl(parsed.s_addr);
            /* Non-NULL output is always accepted by the detailed API. */
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
    }
    response.policy_status = rate_service_classify(response.result);
    response.http_status = to_http_status(response.result);
    *out = response;
    return true;
}

bool request_gateway_handle(rate_service_t *service,
                            const char *client_ipv4,
                            uint32_t requested_tokens,
                            request_gateway_response_t *out)
{
    if (out == NULL) {
        return false;
    }
    /* Legacy callers promise a valid NUL-terminated string. */
    return request_gateway_handle_n(service, client_ipv4,
                                    client_ipv4 != NULL ? strlen(client_ipv4) : 0U,
                                    requested_tokens, out);
}
