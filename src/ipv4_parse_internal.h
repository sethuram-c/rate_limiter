#ifndef RL_IPV4_PARSE_INTERNAL_H
#define RL_IPV4_PARSE_INTERNAL_H

/* Phase 32: one internal IPv4 normalization contract shared by the ingress
 * gateway and CIDR policy. Not installed, and not part of the public API.
 * The bounded entry point never reads outside [input, input + length).
 * Only call the C-string variant with a valid NUL-terminated string.
 */
#include <arpa/inet.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

static inline bool rl_ipv4_parse_span(const char *input, size_t length,
                                      uint32_t *host_order)
{
    enum { RL_IPV4_MAX_TEXT = 15 };
    char terminated[RL_IPV4_MAX_TEXT + 1U];
    struct in_addr parsed;

    if (input == NULL || host_order == NULL || length == 0U ||
        length > RL_IPV4_MAX_TEXT || memchr(input, '\0', length) != NULL) {
        return false;
    }
    memcpy(terminated, input, length);
    terminated[length] = '\0';
    if (inet_pton(AF_INET, terminated, &parsed) != 1) {
        return false;
    }
    *host_order = ntohl(parsed.s_addr);
    return true;
}

static inline bool rl_ipv4_parse_cstr(const char *input, uint32_t *host_order)
{
    size_t length = 0U;
    if (input == NULL || host_order == NULL) {
        return false;
    }
    /* Stop after 15 data characters; no unbounded strlen() needed here. */
    while (length <= 15U && input[length] != '\0') {
        ++length;
    }
    if (length > 15U) {
        return false;
    }
    return rl_ipv4_parse_span(input, length, host_order);
}

#endif
