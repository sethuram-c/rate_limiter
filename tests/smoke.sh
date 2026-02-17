#!/usr/bin/env bash
set -euo pipefail

binary=${1:-./build/ratelimiter}
output="$($binary)"

if ! grep -q 'request=1 decision=ALLOW' <<<"$output"; then
  echo 'smoke test failed: first deterministic request was not allowed' >&2
  exit 1
fi

if ! grep -q 'request=4 decision=DENY' <<<"$output"; then
  echo 'smoke test failed: exhausted bucket did not deny' >&2
  exit 1
fi

if ! grep -q 'after refill tokens=2' <<<"$output"; then
  echo 'smoke test failed: expected refill result was not observed' >&2
  exit 1
fi

live_output="$($binary --live)"
if ! grep -Eq '^mode=live clock=CLOCK_MONOTONIC monotonic_ns=[0-9]+$' <<<"$live_output"; then
  echo 'smoke test failed: live clock metadata missing' >&2
  exit 1
fi

if ! grep -q 'live request=1 decision=ALLOW tokens=2' <<<"$live_output"; then
  echo 'smoke test failed: live request did not consume one token' >&2
  exit 1
fi

if "$binary" --unexpected >/dev/null 2>&1; then
  echo 'smoke test failed: unsupported option unexpectedly succeeded' >&2
  exit 1
fi

echo 'smoke test passed: deterministic and live clock demos behave as expected'
