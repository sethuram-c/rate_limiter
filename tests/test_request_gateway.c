#include "request_gateway.h"

#include <assert.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

/* Fixed callback state is read-only while worker threads run. */
typedef struct {
    uint64_t ms;
    bool fail;
} fake_clock_t;

static bool fake_now(void *context, uint64_t *out)
{
    const fake_clock_t *clock = context;
    if (clock == NULL || out == NULL || clock->fail) {
        return false;
    }
    *out = clock->ms;
    return true;
}

static void test_rejection_and_contracts(void)
{
    rate_service_t service = {0};
    const bucket_policy_t policy = {2U, 100U};
    fake_clock_t clock = {1000U, false};
    request_gateway_response_t reply;
    rate_service_stats_t stats;

    assert(rate_service_init(&service, 2U, &policy, fake_now, &clock));
    assert(!request_gateway_handle(&service, "192.0.2.1", 1U, NULL));
    assert(!request_gateway_handle(NULL, "192.0.2.1", 1U, NULL));
    assert(request_gateway_handle(&service, NULL, 1U, &reply));
    assert(!reply.forwarded && reply.http_status == 400);
    assert(request_gateway_handle(&service, "", 1U, &reply));
    assert(!reply.forwarded && reply.http_status == 400);
    assert(request_gateway_handle(&service, "256.1.1.1", 1U, &reply));
    assert(!reply.forwarded && reply.http_status == 400);
    assert(request_gateway_handle(&service, "192.0.2.1:80", 1U, &reply));
    assert(!reply.forwarded && reply.http_status == 400);
    assert(request_gateway_handle(&service, "1.2.3.4 garbage", 1U, &reply));
    assert(!reply.forwarded && reply.http_status == 400);
    assert(request_gateway_handle(&service, "2001:db8::1", 1U, &reply));
    assert(!reply.forwarded && reply.http_status == 400);
    assert(rate_service_get_stats(&service, &stats));
    assert(stats.allowed == 0U && stats.invalid == 0U &&
           stats.denied == 0U && stats.table_full == 0U);
    assert(ip_atomic_table_count_quiescent(&service.limiter.index) == 0U);

    assert(request_gateway_handle(&service, "0.0.0.0", 1U, &reply));
    assert(reply.forwarded && reply.http_status == 200 &&
           reply.has_remaining && reply.remaining_tokens == 1U);
    assert(request_gateway_handle(&service, "255.255.255.255", 1U, &reply));
    assert(reply.forwarded && reply.http_status == 200);
    assert(request_gateway_handle(&service, "203.0.113.10", 1U, &reply));
    assert(reply.forwarded && reply.http_status == 503 &&
           reply.result == RATE_SERVICE_TABLE_FULL && !reply.has_remaining);
    assert(request_gateway_handle(&service, "0.0.0.0", 0U, &reply));
    assert(reply.forwarded && reply.http_status == 400 &&
           reply.result == RATE_SERVICE_INVALID && !reply.has_remaining);
    assert(rate_service_get_stats(&service, &stats));
    assert(stats.allowed == 2U && stats.table_full == 1U && stats.invalid == 1U);
    rate_service_destroy(&service);
}

static void test_timeline(void)
{
    rate_service_t service = {0};
    const bucket_policy_t policy = {2U, 100U};
    fake_clock_t clock = {1000U, false};
    request_gateway_response_t reply;
    rate_service_stats_t stats;
    size_t i;
    static const struct {
        uint64_t at_ms;
        const char *ip;
        uint32_t tokens;
        bool clock_error;
        rate_service_result_t expected;
        int http;
        bool forwarded;
        bool has_tokens;
        uint16_t remaining;
        bool has_retry;
        uint64_t retry_ms;
    } cases[] = {
        {1000U,"192.0.2.1",1U,false,RATE_SERVICE_ALLOWED,200,true,true,1U,false,0U},
        {1000U,"192.0.2.1",1U,false,RATE_SERVICE_ALLOWED,200,true,true,0U,false,0U},
        {1000U,"192.0.2.1",1U,false,RATE_SERVICE_DENIED,429,true,true,0U,true,100U},
        {1000U,"198.51.100.7",2U,false,RATE_SERVICE_ALLOWED,200,true,true,0U,false,0U},
        {1050U,"192.0.2.1",1U,false,RATE_SERVICE_DENIED,429,true,true,0U,true,50U},
        {1100U,"192.0.2.1",1U,false,RATE_SERVICE_ALLOWED,200,true,true,0U,false,0U},
        {1100U,"not-an-ip",1U,false,RATE_SERVICE_INVALID,400,false,false,0U,false,0U},
        {1100U,"203.0.113.9",1U,false,RATE_SERVICE_TABLE_FULL,503,true,false,0U,false,0U},
        {1100U,"198.51.100.7",1U,true,RATE_SERVICE_CLOCK_ERROR,503,true,false,0U,false,0U},
        {1200U,"198.51.100.7",1U,false,RATE_SERVICE_ALLOWED,200,true,true,1U,false,0U},
        {1200U,"192.0.2.1",0U,false,RATE_SERVICE_INVALID,400,true,false,0U,false,0U},
    };

    assert(rate_service_init(&service, 2U, &policy, fake_now, &clock));
    for (i = 0U; i < sizeof cases / sizeof cases[0]; ++i) {
        clock.ms = cases[i].at_ms;
        clock.fail = cases[i].clock_error;
        assert(request_gateway_handle(&service, cases[i].ip, cases[i].tokens,
                                      &reply));
        assert(reply.result == cases[i].expected);
        assert(reply.http_status == cases[i].http);
        assert(reply.forwarded == cases[i].forwarded);
        assert(reply.policy_status == rate_service_classify(reply.result));
        assert(reply.has_remaining == cases[i].has_tokens);
        if (reply.has_remaining) {
            assert(reply.remaining_tokens == cases[i].remaining);
        }
        assert(reply.has_retry_hint == cases[i].has_retry);
        if (reply.has_retry_hint) {
            assert(reply.retry_after_ms == cases[i].retry_ms);
        }
    }
    assert(ip_atomic_table_count_quiescent(&service.limiter.index) == 2U);
    assert(rate_service_get_stats(&service, &stats));
    assert(stats.allowed == 5U && stats.denied == 2U &&
           stats.table_full == 1U && stats.invalid == 1U &&
           stats.clock_error == 1U);
    rate_service_destroy(&service);
    puts("request gateway timeline: 11 events, 10 service calls, exactly 5 admitted");
}

enum { THREADS = 8, CALLS = 5000 };

typedef struct {
    rate_service_t *service;
    pthread_barrier_t *barrier;
    unsigned index;
    unsigned admitted;
    unsigned denied;
} worker_t;

static void *worker(void *opaque)
{
    worker_t *arg = opaque;
    const char *ips[4] = {"192.0.2.1", "192.0.2.2", "192.0.2.3", "192.0.2.4"};
    unsigned i;
    int rc = pthread_barrier_wait(arg->barrier);
    assert(rc == 0 || rc == PTHREAD_BARRIER_SERIAL_THREAD);
    for (i = 0U; i < CALLS; ++i) {
        request_gateway_response_t response;
        const char *ip = ips[(i + arg->index) % 4U];
        assert(request_gateway_handle(arg->service, ip, 1U, &response));
        assert(response.forwarded);
        if (response.http_status == 200) {
            ++arg->admitted;
        } else {
            assert(response.http_status == 429);
            ++arg->denied;
        }
    }
    return NULL;
}

static void test_concurrent_adapter(void)
{
    const bucket_policy_t policy = {37U, 1000U};
    rate_service_t service = {0};
    fake_clock_t clock = {5000U, false};
    rate_service_stats_t stats;
    pthread_t threads[THREADS];
    worker_t workers[THREADS] = {0};
    pthread_barrier_t barrier;
    unsigned admitted = 0U;
    unsigned denied = 0U;
    unsigned i;

    assert(rate_service_init(&service, 8U, &policy, fake_now, &clock));
    assert(pthread_barrier_init(&barrier, NULL, THREADS + 1U) == 0);
    for (i = 0U; i < THREADS; ++i) {
        workers[i].service = &service;
        workers[i].barrier = &barrier;
        workers[i].index = i;
        assert(pthread_create(&threads[i], NULL, worker, &workers[i]) == 0);
    }
    {
        int rc = pthread_barrier_wait(&barrier);
        assert(rc == 0 || rc == PTHREAD_BARRIER_SERIAL_THREAD);
    }
    for (i = 0U; i < THREADS; ++i) {
        assert(pthread_join(threads[i], NULL) == 0);
        admitted += workers[i].admitted;
        denied += workers[i].denied;
    }
    assert(admitted == 4U * 37U);
    assert(denied == THREADS * CALLS - admitted);
    assert(rate_service_get_stats(&service, &stats));
    assert(stats.allowed == admitted && stats.denied == denied);
    assert(stats.table_full == 0U && stats.clock_error == 0U);
    assert(pthread_barrier_destroy(&barrier) == 0);
    rate_service_destroy(&service);
    printf("request gateway concurrency: %u requests, exactly %u admitted\n",
           THREADS * CALLS, admitted);
}

int main(void)
{
    test_rejection_and_contracts();
    test_timeline();
    test_concurrent_adapter();
    puts("request gateway tests passed");
    return 0;
}
