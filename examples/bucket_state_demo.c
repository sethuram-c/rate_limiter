#include "bucket_state.h"

#include <inttypes.h>
#include <stdio.h>

static void print_state(uint64_t state)
{
    uint64_t anchor;
    uint16_t tokens;
    bucket_state_unpack(state, &anchor, &tokens);
    printf("packed=0x%016" PRIx64 " anchor_ms=%" PRIu64
           " tokens=%" PRIu16 "\n", state, anchor, tokens);
}

int main(void)
{
    bucket_policy_t policy = {3U, 100U};
    uint64_t state;
    bucket_transition_t planned;
    if (!bucket_state_initial(&policy, 1000U, &state)) {
        return 1;
    }
    puts("Phase 6: pure transition DESIGN model, NOT concurrent yet");
    print_state(state);
    if (!bucket_state_plan(&policy, state, 2U, 1000U, &planned)) {
        return 1;
    }
    printf("requested=2 allowed=%s\n", planned.allowed ? "yes" : "no");
    state = planned.next_state;
    print_state(state);
    if (!bucket_state_plan(&policy, state, 2U, 1150U, &planned)) {
        return 1;
    }
    printf("requested=2 allowed=%s\n", planned.allowed ? "yes" : "no");
    print_state(planned.next_state);
    return 0;
}
