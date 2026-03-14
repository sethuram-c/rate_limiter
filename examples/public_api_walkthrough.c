/* Phase 31: complete, installed-library consumer; deterministic and offline.
 * Build with: make docs-test or follow docs/developer_integration_guide.md.
 * Single-threaded fixture_clock_t is NOT appropriate for concurrent workers.
 */
#include "ratelimiter.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

typedef struct {
    uint64_t now_ms;
} fixture_clock_t;

static bool fixture_now(void *context, uint64_t *out)
{
    const fixture_clock_t *clock = context;
    if (clock == NULL || out == NULL) {
        return false;
    }
    *out = clock->now_ms;
    return true;
}

static bool check_http(rate_service_t *service, const char *ip, size_t length,
                       unsigned cost, int status, bool forwarded)
{
    request_gateway_response_t response;
    if (!request_gateway_handle_n(service, ip, length, cost, &response)) {
        return false;
    }
    return response.http_status == status && response.forwarded == forwarded;
}

int main(void)
{
    const rate_config_t config = {
        .slots = 8U,
        .capacity_tokens = 2U,
        .refill_interval_ms = 100U,
        .memory_budget_bytes = 4096U
    };
    /* No trailing NUL: this demonstrates the bounded ingress API. */
    const char ip[] = {'1', '9', '2', '.', '0', '.', '2', '.', '1', '0'};
    fixture_clock_t clock = {1000U};
    rate_config_plan_t plan;
    rate_service_t service = {0};
    request_policy_t policy = {0};
    request_policy_response_t routed;
    rate_service_stats_t stats;
    bool passed = true;

    if (strcmp(rl_version_string(), RL_VERSION_STRING) != 0) {
        fputs("header/archive version mismatch\n", stderr);
        return 1;
    }
    if (rate_config_plan(&config, &plan) != RATE_CONFIG_OK ||
        plan.total_array_bytes > config.memory_budget_bytes) {
        fputs("invalid or over-budget configuration\n", stderr);
        return 1;
    }
    if (!rate_service_init_with_config(&service, &config, fixture_now, &clock)) {
        fputs("rate service initialization failed\n", stderr);
        return 1;
    }

    passed = passed && check_http(&service, ip, sizeof ip, 1U, 200, true);
    passed = passed && check_http(&service, ip, sizeof ip, 1U, 200, true);
    passed = passed && check_http(&service, ip, sizeof ip, 1U, 429, true);
    passed = passed && check_http(&service, "bad", 3U, 1U, 400, false);

    /* Immutable policy: configure rules while no worker threads are active. */
    passed = passed && request_policy_add_rule(
        &policy, "203.0.113.0/24", REQUEST_POLICY_BYPASS);
    passed = passed && request_policy_add_rule(
        &policy, "198.51.100.0/24", REQUEST_POLICY_BLOCK);
    passed = passed && request_policy_handle(
        &policy, &service, "203.0.113.7", 1U, &routed) &&
        routed.route == REQUEST_POLICY_BYPASSED &&
        routed.http_status == 200 && !routed.forwarded;
    passed = passed && request_policy_handle(
        &policy, &service, "198.51.100.7", 1U, &routed) &&
        routed.route == REQUEST_POLICY_BLOCKED &&
        routed.http_status == 403 && !routed.forwarded;

    clock.now_ms = 1100U; /* One complete 100-ms refill interval. */
    passed = passed && check_http(&service, ip, sizeof ip, 1U, 200, true);
    passed = passed && rate_service_get_stats(&service, &stats);
    if (passed) {
        passed = stats.allowed == 3U && stats.denied == 1U &&
                 stats.table_full == 0U && stats.invalid == 0U &&
                 stats.clock_error == 0U;
    }

    /* Do not destroy a service while worker threads could still use it. */
    rate_service_destroy(&service);
    if (!passed) {
        fputs("public API integration FAILED\n", stderr);
        return 1;
    }
    printf("public API walkthrough passed: version=%s allowed=3 denied=1 "
           "malformed=400 bypass=200 blocked=403\n", rl_version_string());
    return 0;
}
