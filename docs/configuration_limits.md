# Phase 13: configuration validation, payload budgets and API contracts

This note belongs in the repository. The separate personal learning PDF is NOT
part of the project or Git history.

## Configuration planning

The opt-in `rate_service_init_with_config()` accepts `rate_config_t` and first
calls `rate_config_plan()`. The planner is pure: it does not allocate, sample a
clock or modify its `out` argument if validation fails. It verifies:

1. `slots` is a nonzero power of two, matching the current linear-probe table.
2. `capacity_tokens` is in `[1, UINT16_MAX]` (the packed 16-bit token field).
3. `refill_interval_ms` is in `[1, BUCKET_STATE_MAX_TIME_MS]` (48-bit time).
4. Each multiplication and addition used for allocation-size estimation fits
   in `size_t`, before evaluating the configured budget.
5. The combined **payload array** allocation estimate is no larger than the
   caller-provided `memory_budget_bytes`.

The computed requirement is exactly

```
slots * sizeof(ip_atomic_slot_t) + slots * sizeof(lf_bucket_t)
```

on the target architecture, but the implementation checks intermediate
multiplications and their addition against `SIZE_MAX` before computing the
arithmetic. The planner returns a typed diagnostic and populated plan only on
success; callers can display the reason using `rate_config_status_string()`.

## Scope and important limitations

- `memory_budget_bytes` is an **array payload** cap, **not** an upper bound on
  resident set size. `malloc` has allocator overhead and other allocations may
  exist elsewhere in the process. For hard process limits, use OS controls.
- `rate_service_init()` and `rate_service_init_monotonic()` are backwards-
  compatible low-level APIs and do **not** enforce `memory_budget_bytes`.
- A valid preflight does not ensure an allocation will succeed. The system may
  still be under memory pressure or fail a lock-free atomic capability check.
- Both tables have a fixed number of slots. Filling them returns TABLE_FULL;
  there is no eviction, resizing, reclaiming or automatic rate-limit bypass.
- Config and all public service fields must remain unchanged during requests.
  Only after stopping/joining every worker may the service be destroyed.
- Rejecting an invalid configuration before the clock callback ensures the
  callback cannot run on known-invalid input. Rejecting a request's invalid
  token amount before a clock read or key claim remains an existing invariant.
- No promise of wait-freedom is made; an injected clock callback can block.

## Example

```c
rate_config_t cfg = {
    .slots = 1024U,
    .capacity_tokens = 100U,
    .refill_interval_ms = 10U,
    .memory_budget_bytes = 64U * 1024U
};
rate_config_plan_t plan;
rate_config_status_t status = rate_config_plan(&cfg, &plan);
if (status != RATE_CONFIG_OK) {
    fprintf(stderr, "invalid config: %s\n", rate_config_status_string(status));
    /* Handle error, do not start worker threads. */
}
```

Check allocation feasibility with `rate_service_init_with_config()` before
starting worker threads; it samples the time, allocates two arrays, prebuilds
all buckets, then makes the service usable. It returns false on allocation,
clock, or lock-free-atomic capability failure. To distinguish a rejected policy
from a runtime initialization failure, invoke `rate_config_plan()` first.

## Verification

```bash
make rate-config-test
make rate-config-demo
make test
```

The new tests cover 2,200 valid plans across sizes/policies, exact-budget
acceptance, one-byte-under rejection, zero/invalid capacities, size overflow,
output preservation on failures, and clock-not-called for bad configs.
