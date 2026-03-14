#!/usr/bin/env bash
# Phase 31: validate the documentation's integration steps using installed files.
set -euo pipefail

repo=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
compiler=${CC:-cc}
archiver=${AR:-ar}
stage=$(mktemp -d)
trap 'rm -rf "$stage"' EXIT

# No /usr changes; entire installation is staged into a private temp folder.
make --no-print-directory -C "$repo" install \
  CC="$compiler" AR="$archiver" DESTDIR="$stage/root" PREFIX=/usr

# Deliberately compile OUTSIDE the checkout with only installed headers.
(
  cd "$stage"
  "$compiler" -D_POSIX_C_SOURCE=200809L -std=c11 \
    -Wall -Wextra -Wpedantic -Werror -O2 \
    -I"$stage/root/usr/include/ratelimiter" \
    "$repo/examples/public_api_walkthrough.c" \
    "$stage/root/usr/lib/libratelimiter.a" -pthread -o walkthrough
  output=$(./walkthrough)
  printf '%s\n' "$output"
  [[ "$output" == 'public API walkthrough passed: version=0.1.0 allowed=3 denied=1 malformed=400 bypass=200 blocked=403' ]]
)
echo 'Phase 31 installed-header documentation walkthrough passed'
