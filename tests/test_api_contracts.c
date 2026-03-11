#include "request_gateway.h"

#include <assert.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

/* A controlled clock provides deterministic results without sleeping. */
typedef struct {
    uint64_t now_ms;
    _Atomic bool fail;
    _Atomic unsigned calls;
} clock_fixture_t;

static bool fixture_now(void *context, uint64_t *now_ms)
{
    clock_fixture_t *clock = context;
    if (clock == NULL || now_ms == NULL) {
        return false;
    }
    (void)atomic_fetch_add_explicit(&clock->calls, 1U, memory_order_relaxed);
    if (atomic_load_explicit(&clock->fail, memory_order_relaxed)) {
        return false;
    }
    *now_ms = clock->now_ms;
    return true;
}

static void assert_ingress_invalid(rate_service_t *service,
                                   const char *data, size_t length)
{
    request_gateway_response_t reply = {0};
    assert(request_gateway_handle_n(service, data, length, 1U, &reply));
    assert(reply.http_status == 400);
    assert(reply.result == RATE_SERVICE_INVALID);
    assert(reply.policy_status == RATE_STATUS_BAD_REQUEST);
    assert(!reply.forwarded && !reply.has_remaining && !reply.has_retry_hint);
    assert(reply.remaining_tokens == 0U && reply.retry_after_ms == 0U);
}

static void test_non_terminated_inputs_and_rejection(void)
{
    static const char raw_ip[] = {'1','9','2','.','0','.','2','.','1'};
    static const char embedded_nul[] = {'1','2','7','.','0','.','0','.','1','\0','x'};
    static const char trailing_space[] = {'1','9','2','.','0','.','2','.','1',' '};
    static const char overlong[] = {'1','9','2','.','0','.','2','.','1','0','0','0','0','0','0','0'};
    static const char max_ipv4[] = {'2','5','5','.','2','5','5','.','2','5','5','.','2','5','5'};
    const bucket_policy_t policy = {2U, 100U};
    clock_fixture_t clock = {1000U, ATOMIC_VAR_INIT(false), ATOMIC_VAR_INIT(0U)};
    rate_service_t service = {0};
    rate_service_stats_t stats;
    request_gateway_response_t reply = {.http_status = 777, .forwarded = true};
    unsigned initial_clock_reads;

    assert(rate_service_init(&service, 2U, &policy, fixture_now, &clock));
    initial_clock_reads = atomic_load(&clock.calls);
    /* On false, the output and all service resources stay unchanged. */
    assert(!request_gateway_handle_n(&service, raw_ip, sizeof raw_ip, 1U, NULL));
    assert(reply.http_status == 777 && reply.forwarded);
    assert_ingress_invalid(&service, NULL, SIZE_MAX);
    assert_ingress_invalid(&service, raw_ip, 0U);
    assert_ingress_invalid(&service, raw_ip, SIZE_MAX);
    assert_ingress_invalid(&service, embedded_nul, sizeof embedded_nul);
    assert_ingress_invalid(&service, trailing_space, sizeof trailing_space);
    assert_ingress_invalid(&service, overlong, sizeof overlong);
    assert_ingress_invalid(&service, raw_ip, sizeof raw_ip - 1U);
    assert_ingress_invalid(&service, "192.0.2.999", 11U);
    assert(atomic_load(&clock.calls) == initial_clock_reads);
    assert(ip_atomic_table_count_quiescent(&service.limiter.index) == 0U);
    assert(rate_service_get_stats(&service, &stats));
    assert(stats.allowed == 0U && stats.invalid == 0U && stats.denied == 0U &&
           stats.clock_error == 0U && stats.table_full == 0U);

    /* Both representations must normalize to identical numeric IP keys. */
    assert(request_gateway_handle_n(&service, raw_ip, sizeof raw_ip, 1U, &reply));
    assert(reply.forwarded && reply.http_status == 200 && reply.remaining_tokens == 1U);
    assert(request_gateway_handle(&service, "192.0.2.1", 1U, &reply));
    assert(reply.forwarded && reply.http_status == 200 && reply.remaining_tokens == 0U);
    assert(request_gateway_handle_n(&service, raw_ip, sizeof raw_ip, 1U, &reply));
    assert(reply.forwarded && reply.http_status == 429 && reply.has_retry_hint &&
           reply.retry_after_ms == 100U && reply.remaining_tokens == 0U);
    assert(request_gateway_handle_n(&service, max_ipv4, sizeof max_ipv4, 1U, &reply));
    assert(reply.forwarded && reply.http_status == 200);
    assert(ip_atomic_table_count_quiescent(&service.limiter.index) == 2U);
    assert(rate_service_get_stats(&service, &stats));
    assert(stats.allowed == 3U && stats.denied == 1U && stats.invalid == 0U);
    rate_service_destroy(&service);
    puts("bounded ingress: malformed buffers are side-effect-free; legacy parity passed");
}

static void test_failure_results_do_not_leak_prior_fields(void)
{
    static const char existing_ip[] = {'1','9','2','.','0','.','2','.','1'};
    static const char new_ip[] = {'1','9','2','.','0','.','2','.','2'};
    const bucket_policy_t policy = {2U, 100U};
    clock_fixture_t clock = {1000U, ATOMIC_VAR_INIT(false), ATOMIC_VAR_INIT(0U)};
    rate_service_t service = {0};
    request_gateway_response_t reply = {0};
    rate_service_diagnostic_t diagnostic = {.result = RATE_SERVICE_ALLOWED};
    rate_service_stats_t stats;
    bool found = false;
    uint64_t anchor = 0U;
    uint16_t remaining = 0U;

    assert(rate_service_init(&service, 1U, &policy, fixture_now, &clock));
    assert(request_gateway_handle_n(&service, existing_ip, sizeof existing_ip,
                                    2U, &reply));
    assert(reply.http_status == 200 && reply.remaining_tokens == 0U);
    assert(request_gateway_handle_n(&service, existing_ip, sizeof existing_ip,
                                    1U, &reply));
    assert(reply.http_status == 429 && reply.has_retry_hint &&
           reply.retry_after_ms == 100U);

    /* A subsequent clock error must clear stale token/retry metadata. */
    atomic_store(&clock.fail, true);
    assert(request_gateway_handle_n(&service, existing_ip, sizeof existing_ip,
                                    1U, &reply));
    assert(reply.http_status == 503 && reply.result == RATE_SERVICE_CLOCK_ERROR &&
           reply.forwarded && !reply.has_remaining && !reply.has_retry_hint &&
           reply.retry_after_ms == 0U && reply.remaining_tokens == 0U);
    assert(ip_rate_limiter_snapshot(&service.limiter, UINT32_C(0xc0000201),
                                    &found, &anchor, &remaining));
    assert(found && anchor == 1000U && remaining == 0U);
    atomic_store(&clock.fail, false);

    assert(request_gateway_handle_n(&service, new_ip, sizeof new_ip, 1U, &reply));
    assert(reply.result == RATE_SERVICE_TABLE_FULL && reply.http_status == 503 &&
           reply.forwarded && !reply.has_remaining && !reply.has_retry_hint);
    assert(request_gateway_handle_n(&service, existing_ip, sizeof existing_ip,
                                    0U, &reply));
    assert(reply.result == RATE_SERVICE_INVALID && reply.http_status == 400 &&
           reply.forwarded && !reply.has_remaining && !reply.has_retry_hint);
    assert(ip_atomic_table_count_quiescent(&service.limiter.index) == 1U);

    /* A detailed API with no output must not attempt or count a request. */
    assert(!rate_service_check_detailed(&service, UINT32_C(0xc0000201), 1U, NULL));
    assert(rate_service_check_detailed(&service, UINT32_C(0xc0000201),
                                       UINT32_MAX, &diagnostic));
    assert(diagnostic.result == RATE_SERVICE_DENIED &&
           diagnostic.has_bucket_state && !diagnostic.retry_possible);
    assert(rate_service_get_stats(&service, &stats));
    assert(stats.allowed == 1U && stats.denied == 2U && stats.table_full == 1U &&
           stats.invalid == 1U && stats.clock_error == 1U);
    rate_service_destroy(&service);
    puts("failure paths: clock errors, full table, invalid amount and stale fields passed");
}

enum { THREADS = 8, REQUESTS_PER_THREAD = 4000 };
static const char ip_a[] = {'1','9','2','.','0','.','2','.','1'};
static const char ip_b[] = {'1','9','2','.','0','.','2','.','2'};

typedef struct {
    rate_service_t *service;
    pthread_barrier_t *barrier;
    unsigned worker_id;
    unsigned admitted;
    unsigned denied;
} worker_args_t;

static void *bounded_worker(void *opaque)
{
    worker_args_t *arg = opaque;
    unsigned i;
    int rc = pthread_barrier_wait(arg->barrier);
    assert(rc == 0 || rc == PTHREAD_BARRIER_SERIAL_THREAD);
    for (i = 0U; i < REQUESTS_PER_THREAD; ++i) {
        request_gateway_response_t reply;
        const char *key = (i + arg->worker_id) % 2U == 0U ? ip_a : ip_b;
        assert(request_gateway_handle_n(arg->service, key, sizeof ip_a,
                                        1U, &reply));
        if (reply.result == RATE_SERVICE_ALLOWED) {
            ++arg->admitted;
        } else {
            assert(reply.result == RATE_SERVICE_DENIED);
            ++arg->denied;
        }
    }
    return NULL;
}

static void test_parallel_bounded_buffers(void)
{
    const bucket_policy_t policy = {37U, 100U};
    clock_fixture_t clock = {2000U, ATOMIC_VAR_INIT(false), ATOMIC_VAR_INIT(0U)};
    rate_service_t service = {0};
    worker_args_t args[THREADS] = {0};
    pthread_t threads[THREADS];
    pthread_barrier_t barrier;
    rate_service_stats_t stats;
    unsigned admitted = 0U, denied = 0U;
    unsigned i;

    assert(rate_service_init(&service, 4U, &policy, fixture_now, &clock));
    assert(pthread_barrier_init(&barrier, NULL, THREADS + 1U) == 0);
    for (i = 0U; i < THREADS; ++i) {
        args[i].service = &service;
        args[i].barrier = &barrier;
        args[i].worker_id = i;
        assert(pthread_create(&threads[i], NULL, bounded_worker, &args[i]) == 0);
    }
    {
        int rc = pthread_barrier_wait(&barrier);
        assert(rc == 0 || rc == PTHREAD_BARRIER_SERIAL_THREAD);
    }
    for (i = 0U; i < THREADS; ++i) {
        assert(pthread_join(threads[i], NULL) == 0);
        admitted += args[i].admitted;
        denied += args[i].denied;
    }
    assert(admitted == 74U && denied == THREADS * REQUESTS_PER_THREAD - 74U);
    assert(rate_service_get_stats(&service, &stats));
    assert(stats.allowed == admitted && stats.denied == denied &&
           stats.invalid == 0U && stats.clock_error == 0U && stats.table_full == 0U);
    assert(ip_atomic_table_count_quiescent(&service.limiter.index) == 2U);
    assert(pthread_barrier_destroy(&barrier) == 0);
    rate_service_destroy(&service);
    printf("bounded ingress concurrency: %u requests, exactly %u admitted\n",
           THREADS * REQUESTS_PER_THREAD, admitted);
}

int main(void)
{
    test_non_terminated_inputs_and_rejection();
    test_failure_results_do_not_leak_prior_fields();
    test_parallel_bounded_buffers();
    puts("phase 28 API contract tests passed");
    return 0;
}
