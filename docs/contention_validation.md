# Phase 8 - Contention analysis and bounded history checking

## Why count attempts?

A CAS failure means this call did not publish its proposed atomic state. It
re-reads the actual state and computes a new transition. A quota denial is a
**successful CAS** that publishes a non-admission decision. They are distinct.
The profiling API writes an attempt count on a successful call. Per-thread
local totals avoid a contested global counter changing the experiment itself.
No fixed retry count is guaranteed; `compare_exchange_weak` can fail spuriously.

## Test design

- Eight threads start at a pthread barrier. Each performs 12,000 requests.
- Ten fresh buckets of 5,000 tokens, fixed at `now_ms = 1000`, no refill.
- Expected total: 960,000 requests, 50,000 grants, 910,000 denials.
- Verify `cas_attempts >= total_calls`, no negative accounting, and final zero.
- A short (8-event) concurrent history captures unique `seq_cst` invocation
  and response event numbers, return decisions, amount, and caller time.
- A depth-first search explores orders respecting `response(A) < invoke(B)`,
  requiring the pure sequential model's answers AND the observed final state.
  Trace samples alternate 1000 and 2000 ms, guaranteeing actual refill.
- It accepts at least one valid history, rejects double-admission on capacity
  one, and rejects a history with a mismatched final state.

## Limitations

Checking 100 small traces is evidence, not proof for every schedule. The
checker shares the pure transition function with the implementation, so it
can miss a common specification bug. Real-time order uses seq_cst counter
stamps and does not imply a precise wall-clock time. `memory_order_relaxed`
remains sufficient for the packed state because the object is safely published
when threads start and policy is immutable until they join.

'Lock-free' is a global-progress statement, not starvation-freedom or
wait-freedom. The weak CAS may need more attempts for individual threads.

## Commands

```bash
make clean && make test
make contention-test
make clean && make CC=clang CFLAGS='-std=c11 -Wall -Wextra -Wpedantic -Werror -O2 -g' test
make clean && make CFLAGS='-std=c11 -Wall -Wextra -Wpedantic -Werror -O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer' LDFLAGS='-fsanitize=address,undefined' contention-test
# Optional: TSAN availability differs by compiler/system:
make clean && make CC=gcc CFLAGS='-std=c11 -Wall -Wextra -Wpedantic -Werror -O1 -g -fsanitize=thread -fno-omit-frame-pointer' LDFLAGS='-fsanitize=thread' contention-test
```

ThreadSanitizer can fail to start on some container/kernel configurations;
record that as an environmental limitation rather than a clean TSAN pass.
Restore the normal build using `make clean && make test`.
