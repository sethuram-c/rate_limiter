#include "linearizability_check.h"

#include <stddef.h>
#include <stdint.h>

/* Exhaustive backtracking is safe for <=10 events (at most 10! leaves). */
static bool search_order(const linear_event_t *history, size_t count,
                         const bucket_policy_t *policy,
                         const uint16_t predecessors[LINEAR_HISTORY_MAX],
                         uint16_t used, uint64_t current_state,
                         uint64_t final_state)
{
    const uint16_t full = (uint16_t)((1U << count) - 1U);
    if (used == full) {
        return current_state == final_state;
    }

    for (size_t i = 0; i < count; ++i) {
        const uint16_t bit = (uint16_t)(1U << i);
        bucket_transition_t transition;
        if ((used & bit) != 0U ||
            (predecessors[i] & used) != predecessors[i]) {
            continue;
        }
        if (!bucket_state_plan(policy, current_state, history[i].amount,
                               history[i].now_ms, &transition) ||
            transition.allowed != history[i].allowed) {
            continue;
        }
        if (search_order(history, count, policy, predecessors,
                         (uint16_t)(used | bit), transition.next_state,
                         final_state)) {
            return true;
        }
    }
    return false;
}

bool linear_history_valid(const linear_event_t *history, size_t count,
                          const bucket_policy_t *policy,
                          uint64_t initial_state, uint64_t final_state)
{
    uint16_t predecessors[LINEAR_HISTORY_MAX] = {0};
    if (history == NULL || policy == NULL || count == 0U ||
        count > LINEAR_HISTORY_MAX) {
        return false;
    }

    for (size_t i = 0; i < count; ++i) {
        if (history[i].invoked >= history[i].returned ||
            history[i].amount == 0U) {
            return false;
        }
        for (size_t j = 0; j < i; ++j) {
            /* Distinct invocation and return event IDs are required. */
            if (history[i].invoked == history[j].invoked ||
                history[i].invoked == history[j].returned ||
                history[i].returned == history[j].invoked ||
                history[i].returned == history[j].returned) {
                return false;
            }
        }
    }
    for (size_t i = 0; i < count; ++i) {
        for (size_t j = 0; j < count; ++j) {
            if (i != j && history[j].returned < history[i].invoked) {
                predecessors[i] |= (uint16_t)(1U << j);
            }
        }
    }
    return search_order(history, count, policy, predecessors, 0U,
                        initial_state, final_state);
}
