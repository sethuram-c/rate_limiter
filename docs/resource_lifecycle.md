# Phase 24: initialization, rollback, ownership and failure injection

**Date:** 2027-01-07. The personal learning PDF is intentionally outside Git.

## Scope

The lock-free request path is unchanged. A new optional `rl_allocator_t`
callback pair is consumed only during initial creation and quiescent teardown.
The default `ip_rate_limiter_init()` and `ip_atomic_table_init()` API signatures
and behavior are retained; callers wanting deterministic memory faults use
`ip_rate_limiter_init_with_allocator()` or
`ip_atomic_table_init_with_allocator()`.

## Ownership tree

    ip_rate_limiter_t (caller owns struct)
    |-- index.slots[]     allocated first, released last
    `-- buckets[]         allocated second, released first

The two arrays must use the matching deallocator that allocated them. Their
allocator **function pointers and context are copied**, not borrowed as an
`rl_allocator_t *` object. The context object itself must remain alive until
destruction. An allocator must return suitably aligned memory.

## All-or-nothing initialization

- Validate pointer, zero state, capacity, policy and clock range before any
  allocation. Both stored allocator callbacks must be supplied together.
- Allocate + initialize the atomic key array. If that fails, no resources
  remain owned by the limiter.
- Allocate the bucket array, honoring `_Alignof(lf_bucket_t)` (8 or 64 in
  our verified builds). If it fails, release the key array immediately.
- Initialize every bucket before making any key available. If any bucket
  initialization fails, release both arrays.
- Only after success publish `limiter->buckets` and store the allocator copy.
  No request may start until this initialization has returned successfully.

**No concurrent init/destroy:** The user MUST stop/join all worker threads
before `ip_rate_limiter_destroy()`. Atomic operations on the array do not make
freeing it concurrent with reads safe. Calling destroy twice on a quiescent
instance is supported; calling it while a thread is in check/snapshot is not.

## Deterministic fault matrix

| Fault | Expected allocations | Expected releases | Result |
| --- | --- | --- | --- |
| First allocation returns NULL | 0 succeed | 0 | Clean zero state |
| Second allocation returns NULL | 1 succeeds | 1 | Clean zero state |
| Bad bucket policy | 2 succeed | 2 | Clean zero state |
| Successful creation | 2 succeed | 2 after destroy | Requests work |
| Reinit live object | 0 new | 0 new | Rejected unchanged |
| Double destroy | 0 new | 0 new | Safe quiescent no-op |
| Padded layout | 2 succeed | 2 after destroy | 64-byte alignment |

The fault-injection counter is deterministic: fail on exactly allocation #1
or #2. This is better for validating cleanup than waiting for the operating
system to naturally run out of memory, which is not repeatable or safe.

## Linux commands

```bash
make clean
make lifecycle-test
make lifecycle-test-padded
make ip-rate-test ip-atomic-test rate-service-test
make test

# Rebuild using Clang rather than reusing GCC binaries:
make -B CC=clang lifecycle-test lifecycle-test-padded

# Targeted ASan+UBSan build and run:
cc -D_POSIX_C_SOURCE=200809L -Iinclude -std=c11 \
  -Wall -Wextra -Wpedantic -Werror -O1 -g -fsanitize=address,undefined \
  -fno-omit-frame-pointer -pthread \
  tests/test_resource_lifecycle.c src/ip_rate_limiter.c \
  src/ip_atomic_table.c src/rate_allocator.c src/ip_table_plan.c \
  src/lockfree_bucket.c src/bucket_state.c \
  -o build/test_resource_lifecycle_sanitized
ASAN_OPTIONS=detect_leaks=1 ./build/test_resource_lifecycle_sanitized
```

AddressSanitizer detects use-after-free and some leaks, but passing sanitizer
runs is not a formal proof. Leak detection can depend on toolchain/runtime.

## Design and performance implications

- `rl_allocator_alloc()` handles default `malloc` for ordinary alignment,
  and `aligned_alloc` when the requested alignment exceeds `max_align_t`.
  `aligned_alloc` requires bytes to be an alignment multiple; our bucket
  array meets this requirement because each bucket has aligned stride.
- A custom allocator must match all requested alignment requirements and
  pair `allocate` with `deallocate`; allocating with a callback and freeing
  with default `free()` would be an ownership error.
- There is no memory allocation inside `ip_rate_limiter_check` or the CAS
  operation. Our fault-injection callback tests only the lifecycle path.
- The allocator contract is **not** a concurrent memory reclamation scheme:
  slots are insert-only, their storage never moves and shutdown is quiescent.
- `_Alignof(lf_bucket_t)` changes with `RL_CACHELINE_PADDED`. Rebuild the
  entire translation-unit set consistently when toggling that flag.
- Bounds from `rate_config_plan()` still describe the two main array payloads,
  not all process memory or custom allocator metadata.

## What this does not promise

It does not recover from an external caller freeing memory incorrectly or a
user-provided allocator that violates its alignment contract. It does not
provide thread-safe initialization, concurrent destruction, an HTTP server,
or dynamic IP eviction. The request path's lock-free guarantee still depends
on the platform's atomic capability and on the distinction between the
in-process bucket operation and the external service clock provider.

## Completion criteria

1. Both failure points roll back without an outstanding allocation.
2. A failed limiter can be initialized again successfully.
3. Requests work after successful retry, and reinitialization while live
   is rejected without changes.
4. `destroy()` leaves a zero-state object and does not double-free.
5. Default and padded layouts pass focused tests, including sanitizer checks.
6. Learning PDF is kept *outside* the repository; no generated binaries
   or temporary benchmark output is committed.
