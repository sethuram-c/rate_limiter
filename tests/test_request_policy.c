#include "request_policy.h"

#include <assert.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

typedef struct { uint64_t ms; bool fail; } fake_clock_t;

static bool fake_now(void *ctx, uint64_t *now)
{
    const fake_clock_t *clock = ctx;
    if (clock == NULL || now == NULL || clock->fail) return false;
    *now = clock->ms;
    return true;
}

static void test_rules_and_failures(void)
{
    request_policy_t policy = {0};
    request_policy_response_t reply;
    const request_policy_response_t untouched = {.http_status = 777};
    rate_service_t service = {0};
    rate_service_stats_t stats;
    const bucket_policy_t bucket = {2U, 100U};
    fake_clock_t clock = {1000U, false};
    const char *bad[] = {NULL, "", "192.0.2.1", "192.0.2.1/",
                         "192.0.2.1/-1", "192.0.2.1/33", "192.0.2.1/99999999999",
                         "192.0.2.1/1garbage", "192.0.2.1/24/1",
                         "300.1.2.3/24", "192.0.2.1/ 24", "2001:db8::1/128"};
    size_t i;

    for (i = 0U; i < sizeof(bad) / sizeof(bad[0]); ++i) {
        assert(!request_policy_add_rule(&policy, bad[i], REQUEST_POLICY_BLOCK));
        assert(policy.count == 0U);
    }
    assert(!request_policy_add_rule(&policy, "192.0.2.0/24", (request_policy_action_t)91));
    assert(!request_policy_add_rule(NULL, "192.0.2.0/24", REQUEST_POLICY_BLOCK));
    assert(request_policy_add_rule(&policy, "0.0.0.0/0", REQUEST_POLICY_BLOCK));
    assert(request_policy_add_rule(&policy, "192.0.2.37/24", REQUEST_POLICY_BYPASS));
    assert(policy.rules[1].network == UINT32_C(0xC0000200));
    assert(request_policy_add_rule(&policy, "192.0.2.42/32", REQUEST_POLICY_BLOCK));
    assert(request_policy_add_rule(&policy, "192.0.2.42/32", REQUEST_POLICY_BYPASS));

    assert(rate_service_init(&service, 2U, &bucket, fake_now, &clock));
    reply = untouched;
    assert(!request_policy_handle(NULL, &service, "192.0.2.5", 1U, &reply));
    assert(reply.http_status == untouched.http_status);
    assert(!request_policy_handle(&policy, NULL, "192.0.2.5", 1U, &reply));
    assert(reply.http_status == untouched.http_status);
    assert(!request_policy_handle(&policy, &service, "192.0.2.5", 1U, NULL));

    assert(request_policy_handle(&policy, &service, "192.0.2.7", 1U, &reply));
    assert(reply.route == REQUEST_POLICY_BYPASSED && reply.http_status == 200);
    assert(reply.rule_matched && reply.matched_prefix == 24U && !reply.forwarded);
    assert(!reply.has_remaining && !reply.has_retry_hint && !reply.has_service_result);
    assert(request_policy_handle(&policy, &service, "192.0.2.42", 1U, &reply));
    assert(reply.route == REQUEST_POLICY_BLOCKED && reply.http_status == 403);
    assert(reply.rule_matched && reply.matched_prefix == 32U && !reply.forwarded);
    assert(request_policy_handle(&policy, &service, "198.51.100.1", 1U, &reply));
    assert(reply.route == REQUEST_POLICY_BLOCKED && reply.matched_prefix == 0U);
    assert(!reply.forwarded);

    /* Even a matching exemption cannot allow malformed requests or zero cost. */
    assert(request_policy_handle(&policy, &service, "192.0.2.7", 0U, &reply));
    assert(reply.route == REQUEST_POLICY_DELEGATED && reply.forwarded);
    assert(reply.http_status == 400 && reply.service_result == RATE_SERVICE_INVALID);
    assert(request_policy_handle(&policy, &service, "192.0.2.999", 1U, &reply));
    assert(reply.route == REQUEST_POLICY_DELEGATED && !reply.forwarded);
    assert(reply.http_status == 400 && !reply.has_service_result);
    assert(rate_service_get_stats(&service, &stats));
    assert(stats.invalid == 1U && stats.allowed == 0U && stats.denied == 0U);
    assert(ip_atomic_table_count_quiescent(&service.limiter.index) == 0U);

    rate_service_destroy(&service);
    puts("policy rules: longest-prefix, DENY ties, /0, /32, validation passed");
}

static void test_delegation_and_full_capacity(void)
{
    request_policy_t policy = {0};
    request_policy_response_t reply;
    rate_service_t service = {0};
    rate_service_stats_t stats;
    const bucket_policy_t bucket = {2U, 100U};
    fake_clock_t clock = {1000U, false};
    size_t i;

    assert(request_policy_add_rule(&policy, "192.0.2.0/24", REQUEST_POLICY_BYPASS));
    assert(request_policy_add_rule(&policy, "192.0.2.42/32", REQUEST_POLICY_BLOCK));
    for (i = policy.count; i < REQUEST_POLICY_MAX_RULES; ++i) {
        assert(request_policy_add_rule(&policy, "203.0.113.0/24", REQUEST_POLICY_BLOCK));
    }
    assert(policy.count == REQUEST_POLICY_MAX_RULES);
    assert(!request_policy_add_rule(&policy, "198.51.100.0/24", REQUEST_POLICY_BYPASS));
    assert(policy.count == REQUEST_POLICY_MAX_RULES);

    assert(rate_service_init(&service, 2U, &bucket, fake_now, &clock));
    assert(request_policy_handle(&policy, &service, "198.51.100.5", 1U, &reply));
    assert(reply.forwarded && reply.has_service_result && reply.http_status == 200);
    assert(reply.service_result == RATE_SERVICE_ALLOWED && reply.remaining_tokens == 1U);
    assert(request_policy_handle(&policy, &service, "198.51.100.5", 2U, &reply));
    assert(reply.forwarded && reply.http_status == 429 && reply.has_retry_hint);
    assert(reply.retry_after_ms == 100U && reply.remaining_tokens == 1U);
    assert(request_policy_handle(&policy, &service, "198.51.100.5", 1U, &reply));
    assert(reply.http_status == 200 && reply.remaining_tokens == 0U);
    assert(request_policy_handle(&policy, &service, "198.51.100.5", 1U, &reply));
    assert(reply.http_status == 429 && reply.retry_after_ms == 100U);
    assert(request_policy_handle(&policy, &service, "192.0.2.42", 1U, &reply));
    assert(reply.http_status == 403 && !reply.forwarded);
    assert(request_policy_handle(&policy, &service, "192.0.2.43", 500U, &reply));
    assert(reply.http_status == 200 && !reply.forwarded); /* explicit exemption */
    assert(request_policy_handle(&policy, &service, "198.51.100.6", 1U, &reply));
    assert(reply.http_status == 200 && reply.forwarded);
    assert(request_policy_handle(&policy, &service, "198.51.100.7", 1U, &reply));
    assert(reply.http_status == 503 && reply.service_result == RATE_SERVICE_TABLE_FULL);

    assert(rate_service_get_stats(&service, &stats));
    assert(stats.allowed == 3U && stats.denied == 2U && stats.table_full == 1U);
    assert(stats.invalid == 0U && stats.clock_error == 0U);
    rate_service_destroy(&service);
    puts("policy delegation: 200/403/429/503, refill hint, no counter pollution passed");
}

enum { THREADS = 8, CALLS = 5000 };

typedef struct {
    const request_policy_t *policy;
    rate_service_t *service;
    pthread_barrier_t *barrier;
    unsigned idx;
    unsigned bypassed;
    unsigned blocked;
    unsigned admitted;
    unsigned throttled;
    unsigned malformed;
} worker_t;

static void *worker(void *opaque)
{
    worker_t *w = opaque;
    const char *ips[] = {"192.0.2.10", "192.0.2.20",
                         "198.51.100.9", "not-an-ip"};
    unsigned i;
    int rc = pthread_barrier_wait(w->barrier);
    assert(rc == 0 || rc == PTHREAD_BARRIER_SERIAL_THREAD);
    for (i = 0U; i < CALLS; ++i) {
        request_policy_response_t out;
        unsigned k = (i + w->idx) % 4U;
        assert(request_policy_handle(w->policy, w->service, ips[k], 1U, &out));
        if (k == 0U) {
            assert(out.http_status == 200 && out.route == REQUEST_POLICY_BYPASSED);
            ++w->bypassed;
        } else if (k == 1U) {
            assert(out.http_status == 403 && out.route == REQUEST_POLICY_BLOCKED);
            ++w->blocked;
        } else if (k == 2U) {
            assert(out.forwarded && out.route == REQUEST_POLICY_DELEGATED);
            if (out.http_status == 200) ++w->admitted;
            else { assert(out.http_status == 429); ++w->throttled; }
        } else {
            assert(out.http_status == 400 && !out.forwarded);
            ++w->malformed;
        }
    }
    return NULL;
}

static void test_concurrent_policy(void)
{
    request_policy_t policy = {0};
    rate_service_t service = {0};
    const bucket_policy_t bucket = {41U, 1000U};
    fake_clock_t clock = {5000U, false};
    pthread_t tids[THREADS];
    worker_t w[THREADS] = {0};
    pthread_barrier_t barrier;
    rate_service_stats_t stats;
    unsigned i, bypassed = 0U, blocked = 0U, admitted = 0U;
    unsigned throttled = 0U, malformed = 0U;

    assert(request_policy_add_rule(&policy, "192.0.2.10/32", REQUEST_POLICY_BYPASS));
    assert(request_policy_add_rule(&policy, "192.0.2.20/32", REQUEST_POLICY_BLOCK));
    assert(rate_service_init(&service, 4U, &bucket, fake_now, &clock));
    assert(pthread_barrier_init(&barrier, NULL, THREADS + 1U) == 0);
    for (i = 0U; i < THREADS; ++i) {
        w[i].policy = &policy;
        w[i].service = &service;
        w[i].barrier = &barrier;
        w[i].idx = i;
        assert(pthread_create(&tids[i], NULL, worker, &w[i]) == 0);
    }
    {
        int rc = pthread_barrier_wait(&barrier);
        assert(rc == 0 || rc == PTHREAD_BARRIER_SERIAL_THREAD);
    }
    for (i = 0U; i < THREADS; ++i) {
        assert(pthread_join(tids[i], NULL) == 0);
        bypassed += w[i].bypassed;
        blocked += w[i].blocked;
        admitted += w[i].admitted;
        throttled += w[i].throttled;
        malformed += w[i].malformed;
    }
    assert(bypassed == 10000U && blocked == 10000U && malformed == 10000U);
    assert(admitted == 41U && throttled == 9959U);
    assert(rate_service_get_stats(&service, &stats));
    assert(stats.allowed == admitted && stats.denied == throttled);
    assert(stats.table_full == 0U && stats.invalid == 0U && stats.clock_error == 0U);
    assert(ip_atomic_table_count_quiescent(&service.limiter.index) == 1U);
    assert(pthread_barrier_destroy(&barrier) == 0);
    rate_service_destroy(&service);
    printf("request policy concurrency: %u requests: 10000 bypass, 10000 block, "
           "10000 malformed, %u admitted, %u throttled\n",
           THREADS * CALLS, admitted, throttled);
}

int main(void)
{
    test_rules_and_failures();
    test_delegation_and_full_capacity();
    test_concurrent_policy();
    puts("request policy tests passed");
    return 0;
}
