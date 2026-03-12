# Phase 29: C11 static library packaging and external integration

`make library` builds `build/libratelimiter.a` from the 12 rate-limiting
production modules. It deliberately excludes `src/main.c`, benchmarks,
unit tests, the mutex comparison, the traffic generator and atomic teaching
lab. This keeps the distributable API centered on the usable system.

## Supported environments and guarantees

- Linux/POSIX with a C11-capable compiler and GNU Make / ar.
- Public source-level API: `#include <ratelimiter/ratelimiter.h>` with the prefix include root,
  or `#include "ratelimiter.h"` with the namespaced include directory.
- Static linking. No `.so`, symbol-versioning, `pkg-config`, semantic version
  guarantee, stable ABI, or cross-platform portability is claimed here.
- The API exposes struct types and C11 `_Atomic` members through headers;
  users must build against compatible C compiler/target options.
- Link pthread support, and (if a particular 32-bit target requires it)
  platform-specific atomic-runtime libraries.
- Application callbacks must be safe for concurrent calls; service lifecycle
  must be quiescent across `init` and `destroy`.
- The in-process gateway is NOT a network listener or HTTP server. Do not
  trust forwarded addresses without an authenticated proxy boundary.

## From repo to staged installation

```bash
make clean
make help
make library
ar t build/libratelimiter.a
nm -g --defined-only build/libratelimiter.a | grep rate_service_check
make install DESTDIR="$PWD/build/stage" PREFIX=/usr
find build/stage/usr -type f | sort
```

`DESTDIR` is a filesystem staging root; `PREFIX` specifies the path inside
that root. The actual created paths above are:

```
build/stage/usr/lib/libratelimiter.a
build/stage/usr/include/ratelimiter/ratelimiter.h
build/stage/usr/include/ratelimiter/rate_service.h
... remaining transitive and compatibility headers ...
```

Both `PREFIX=/usr` and `PREFIX=/usr/local` are valid; the install step only
modifies your system if you intentionally omit `DESTDIR` and have permissions.
It is safer to test staging than to run `sudo make install`.

## Consumer build from staged files

```bash
make package-test
cc -D_POSIX_C_SOURCE=200809L -std=c11 -Wall -Wextra -Wpedantic \
   -I"$PWD/build/stage/usr/include/ratelimiter" \
   examples/external_consumer.c \
   "$PWD/build/stage/usr/lib/libratelimiter.a" \
   -pthread -o build/external_consumer
./build/external_consumer
```

The test stages an installation into a temporary directory and compiles
`examples/external_consumer.c` **with the staged headers only** and with no
source `.c` files on the link line. This is the property the normal in-tree
test suite does not establish.

## Why header dependencies are all installed

`ratelimiter.h` is the recommended umbrella; legacy public headers include
other headers by quoted relative names. The package therefore installs every
existing `include/*.h` underneath `include/ratelimiter/` to avoid broken
transitive includes. A future phase can separate internal headers and
provide an opaque-handle API, but doing so now would break existing clients.

## Notes on archive and link order

A `.a` archive contains object files; unlike a `.so` it is not loaded by the
runtime linker as a separate shared library. `ar rcs` builds the archive and
symbol index. `nm` inspects exported symbols; `ar t` lists members.

For GNU-style linker resolution, put library **after** the consumer object:

```
cc consumer.o -L/path/to/lib -lratelimiter -pthread -o consumer
```

The archive contains the rate limiter only; a caller using the mutex baseline
still needs the separate test/baseline compilation paths. Static linking
does not prove concurrency correctness; the existing sanitizer, contention,
linearizability, and API-contract suites remain essential.

## Limitations to state in the README and in interviews

- This is a C11/Linux in-process static library.
- No socket handling, TLS, persistence, dynamic resizing, deletion, or
  process-shared/global distributed quota semantics.
- A successful atomic operation does not imply that user-provided clock
  callbacks are lock-free.
- Cacheline padding remains opt-in and affects binary layout; changing it
  between an installed library and compiled client is unsupported.
- Exporting the struct definition exposes its binary layout. An opaque
  handle would be needed for a stable ABI and easier binary compatibility.
