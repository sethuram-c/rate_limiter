# Developer integration guide — build and consume the C11 static library

This is a **reproducible offline, non-root** integration path for Fedora and other Linux environments with `make`, a C11 compiler, `ar`, `pthread` support, and Python 3 for repository tests. The source must be checked out locally. The documented program is `examples/public_api_walkthrough.c`.

## 1. From source to installed library

Run these commands from the repository root:

```bash
make clean
make CC=gcc library
ar t build/libratelimiter.a
nm -g --defined-only build/libratelimiter.a | grep rl_version_string
make install DESTDIR="$PWD/build/stage" PREFIX=/usr
find build/stage/usr -type f | sort
```

The archive holds production object files, not the CLI's `main` or test/benchmark binaries. `ar t` lists archive members, while `nm -g --defined-only` lists exported definitions. `DESTDIR` keeps files under your build directory, not `/usr`; `PREFIX=/usr` controls their **relative layout** under that stage.

**Never run `sudo make install` to try this example.** A staged install avoids system changes.

## 2. Compile an independent consumer

The checked-in `public_api_walkthrough.c` includes `"ratelimiter.h"` with its installed namespaced include folder and accesses **only installed headers**. Use:

```bash
cc -D_POSIX_C_SOURCE=200809L -std=c11 \
  -Wall -Wextra -Wpedantic -Werror -O2 \
  -I"$PWD/build/stage/usr/include/ratelimiter" \
  examples/public_api_walkthrough.c \
  "$PWD/build/stage/usr/lib/libratelimiter.a" \
  -pthread -o build/public_api_walkthrough
./build/public_api_walkthrough
```

Expected exact output on success:

```
public API walkthrough passed: version=0.1.0 allowed=3 denied=1 malformed=400 bypass=200 blocked=403
```

**Why link order matters:** the consumer source/object appears before the static archive, so its undefined references can pull necessary archive members during the link. `-pthread` enables pthread linkage/flags. This is a static, in-process C library, not an HTTP daemon. The umbrella header may also be included as `<ratelimiter/ratelimiter.h>` if the compiler include path points at the parent installed include directory.

The automated equivalent is:

```bash
make docs-test
```

That target validates document contracts, stages a temporary installation, compiles the sample against installed headers, runs it, and checks the exact result. Generated binaries and staging folders belong under `build/` or temporary directories and are **not** committed.

## 3. Code walkthrough: seven stages

The full, maintained example lives at [`examples/public_api_walkthrough.c`](../examples/public_api_walkthrough.c).

1. **Version check:** `strcmp(rl_version_string(), RL_VERSION_STRING)` rejects mismatched header/library version strings. It is a diagnostic, not a robust stable ABI scheme.
2. **Plan configuration:** `rate_config_plan()` checks slots, capacity, refill interval and array payload budget *without allocation or clock sampling*.
3. **Initialize:** `rate_service_t service = {0}` and `rate_service_init_with_config()` prepare full per-IP buckets before requests can publish keys.
4. **Bounded ingress:** `request_gateway_handle_n()` accepts a deliberately unterminated, 10-byte address. At `t=1000ms`, a capacity-two bucket allows exactly two one-token requests and denies the third with HTTP-style 429.
5. **Reject untrusted input:** an invalid address returns 400 without touching service counters; it is an ingress-layer rejection, not a rate-service `INVALID` count.
6. **Policy paths:** immutable CIDR BYPASS and BLOCK examples return illustrative 200 and 403, respectively. They do not claim IP slots or modify rate-service outcome counters.
7. **Refill and cleanup:** move the single-threaded fake clock to `1100ms`; the next request is allowed. Read counters (three allowed, one denied), then `rate_service_destroy()` **after** work stops.

The demonstration uses `fixture_clock_t` which **is not thread-safe** because it stores a plain mutable `uint64_t`. Only change it in a single-threaded test/demo. Concurrent production callbacks must be thread-safe, or use the monotonic initializer.

## 4. Practical integration choices

| Application need | Recommended API |
|---|---|
| Admission for already normalized IPv4 integer | `rate_service_check()` |
| Full decision information | `rate_service_check_detailed()` |
| Live Linux time without a custom clock | `rate_service_init_monotonic()` |
| Bound non-NUL-terminated buffer | `request_gateway_handle_n()` |
| Conventional dotted IPv4 string | `request_gateway_handle()` |
| Static CIDR allow/bypass or deny/block | `request_policy_add_rule()` and `request_policy_handle()` |
| Strict payload budget / invalid config explanation | `rate_config_plan()`, `rate_service_init_with_config()` |
| Final request outcome accounting | `rate_service_get_stats()` after threads join |

A real server must obtain a trustworthy client IP (e.g. from a directly authenticated connection or a verified proxy boundary), parse protocol framing and map status numbers into actual responses **outside** this library.

## 5. Resource and concurrency checklist

- Is the service zero-initialized and built before workers start?
- Are all configuration values checked, and is the memory budget understood as **payload only**, not total RSS?
- Is the same installed header set used for compilation and linking?
- Are custom callbacks safe when called concurrently? Are clock values monotonically non-decreasing and in **milliseconds**?
- Is policy configuration frozen before workers begin?
- Is there a strategy for hash-table exhaustion without deletion/resize?
- Are malformed IPs and bypass/block actions tracked at the **gateway** rather than mistakenly inside limiter counters?
- Does shutdown stop and join **every** user thread before destroying the service?

## 6. Verification and diagnosis on Fedora

```bash
make clean
make CC=gcc library
make docs-test
make package-test
make CC=gcc test
make static-check
make audit-asan
```

For an independent compiler pass:

```bash
make clean
make CC=clang docs-test
make CC=clang test
```

Debug the standalone consumer:

```bash
cc -D_POSIX_C_SOURCE=200809L -std=c11 -g -O0 \
  -I"$PWD/build/stage/usr/include/ratelimiter" \
  examples/public_api_walkthrough.c \
  "$PWD/build/stage/usr/lib/libratelimiter.a" \
  -pthread -o build/public_api_debug
gdb --args build/public_api_debug
```

Suggested GDB commands: `break main`, `run`, `next`, `print clock.now_ms`, `print stats.allowed`, `bt`, `quit`. Remember that the archive was built with its own `CFLAGS`: compile it with `-g -O0` as well if you need to step reliably into every implementation detail.

**Common errors:**

- `fatal error: ratelimiter.h: No such file`: for the walkthrough use `-I.../usr/include/ratelimiter`; for applications including `<ratelimiter/ratelimiter.h>`, use `-I.../usr/include` instead.
- `undefined reference to rate_service_init...`: link `libratelimiter.a` *after* the consumer object/source.
- `pthread` or atomics link failure: include `-pthread`; some targets additionally require their platform atomics runtime.
- `rate_service_init_with_config` returns false: verify policy, timestamp range, allocation support, lock-free atomics and the resource budget.
- Address blocked by CIDR rule: 403 is distinct from token denial 429.
- `make docs-test` fails after editing docs: confirm command snippets and expected API symbols still describe the installed library and rerun checks.

## 7. Scope and release status

This is **experimental v0.1.0**, still marked *Unreleased*. It is not a stable ABI, shared library, network server, IPC/global limiter or security boundary. Do not quote throughput measurements without machine-specific methodology. The learning PDF for each phase is personal study material and remains **outside** this repository.

Further reading: [Public API reference](public_api_reference.md), [Library packaging](library_packaging.md), and [Release readiness](release_readiness.md).
