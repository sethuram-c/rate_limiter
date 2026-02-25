#include "ip_rate_limiter.h"

#include <inttypes.h>
#include <stdio.h>

static const char *decision_name(ip_rate_decision_t decision)
{
    switch (decision) {
    case IP_RATE_ALLOWED: return "ALLOW";
    case IP_RATE_DENIED: return "DENY";
    case IP_RATE_TABLE_FULL: return "TABLE_FULL";
    case IP_RATE_INVALID: return "INVALID";
    }
    return "UNKNOWN";
}

int main(void)
{
    const bucket_policy_t policy = { 2U, 100U };
    const uint32_t alice = UINT32_C(0xc0000201);
    const uint32_t bob = UINT32_C(0xc0000202);
    const uint32_t ips[] = { alice, alice, alice, bob, bob, alice };
    const uint64_t times[] = { 1000U, 1000U, 1000U, 1000U, 1000U, 1100U };
    ip_rate_limiter_t limiter = {0};
    size_t i;

    if (!ip_rate_limiter_init(&limiter, 8U, &policy, 1000U)) {
        fputs("per-IP limiter initialization failed\n", stderr);
        return 1;
    }
    for (i = 0; i < sizeof(ips) / sizeof(ips[0]); ++i) {
        ip_rate_decision_t decision = ip_rate_limiter_check(
            &limiter, ips[i], 1U, times[i]);
        printf("ip=0x%08" PRIx32 " t=%" PRIu64 " decision=%s\n",
               ips[i], times[i], decision_name(decision));
    }
    printf("unique IPs=%zu\n",
           ip_atomic_table_count_quiescent(&limiter.index));
    ip_rate_limiter_destroy(&limiter);
    return 0;
}
