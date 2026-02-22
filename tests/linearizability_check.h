#ifndef LINEARIZABILITY_CHECK_H
#define LINEARIZABILITY_CHECK_H

#include "bucket_state.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* A deliberately small exhaustive test oracle (MAX_EVENTS <= 10). */
#define LINEAR_HISTORY_MAX 10U

typedef struct {
    uint64_t invoked;   /* unique operation-invocation event number */
    uint64_t returned;  /* unique operation-response event number */
    uint64_t now_ms;
    uint32_t amount;
    bool allowed;
} linear_event_t;

/*
 * Accept iff some legal serial order:
 * - preserves every response-before-another-invocation constraint,
 * - matches every recorded admission decision, and
 * - ends at the observed final packed state.
 * Input history must contain 1..MAX_EVENTS complete, well-formed operations.
 * This is bounded test code, not a proof for all possible executions.
 */
bool linear_history_valid(const linear_event_t *history, size_t count,
                          const bucket_policy_t *policy,
                          uint64_t initial_state, uint64_t final_state);

#endif
