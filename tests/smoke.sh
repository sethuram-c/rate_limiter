#!/usr/bin/env bash
set -euo pipefail

binary=${1:-./build/ratelimiter}
output="$($binary)"

if ! grep -q 'request=1 decision=ALLOW' <<<"$output"; then
  echo "smoke test failed: first request was not allowed" >&2
  exit 1
fi

if ! grep -q 'request=4 decision=DENY' <<<"$output"; then
  echo "smoke test failed: exhausted bucket did not deny" >&2
  exit 1
fi

if ! grep -q 'after refill tokens=2' <<<"$output"; then
  echo "smoke test failed: expected refill result was not observed" >&2
  exit 1
fi

echo "smoke test passed: deterministic token bucket demo behaves as expected"
