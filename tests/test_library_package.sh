#!/usr/bin/env bash
# Phase 29: build, stage, and consume the package without source-tree headers.
set -euo pipefail

repo=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
compiler=${CC:-cc}
archiver=${AR:-ar}
stage=$(mktemp -d)
trap 'rm -rf "$stage"' EXIT

# Ensure the documented help interface remains executable.
make --no-print-directory -s -C "$repo" help >/dev/null

# DESTDIR stages an installation without modifying /usr or /usr/local.
make --no-print-directory -C "$repo" install \
  CC="$compiler" AR="$archiver" DESTDIR="$stage/root" PREFIX=/usr

header="$stage/root/usr/include/ratelimiter/ratelimiter.h"
archive="$stage/root/usr/lib/libratelimiter.a"
[[ -f "$header" && -s "$archive" ]] || {
  echo 'packaging test: missing header or library' >&2
  exit 1
}
# Detect accidental archive emptiness without assuming exact object counts.
"$archiver" t "$archive" | grep -q 'rate_service.o'
"$archiver" t "$archive" | grep -q 'request_gateway.o'

# Compile outside the repository directory. No -Iinclude or source .c files.
(
  cd "$stage"
  "$compiler" -D_POSIX_C_SOURCE=200809L -std=c11 \
    -Wall -Wextra -Wpedantic -Werror -O2 \
    -I"$stage/root/usr/include/ratelimiter" \
    "$repo/examples/external_consumer.c" \
    "$archive" -pthread -o external_consumer
  ./external_consumer
)
echo 'Phase 29 package test passed: staged install, archive, installed headers, external link'
