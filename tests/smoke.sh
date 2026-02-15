#!/usr/bin/env bash
set -euo pipefail

readonly binary="${1:-./build/ratelimiter}"
readonly expected="lockfree-rate-limiter: project scaffold ready"

if [[ ! -x "$binary" ]]; then
    printf 'error: executable not found: %s\n' "$binary" >&2
    exit 1
fi

actual="$($binary)"

if [[ "$actual" != "$expected" ]]; then
    printf 'error: unexpected output\nexpected: %s\nactual:   %s\n' \
        "$expected" "$actual" >&2
    exit 1
fi

printf 'smoke test passed: %s\n' "$actual"
