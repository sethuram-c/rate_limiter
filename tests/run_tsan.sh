#!/usr/bin/env bash
# Phase 25: distinguish an unsupported TSAN runtime from a detected data race.
set -euo pipefail
if [[ $# -ne 1 ]]; then
  echo "usage: $0 path/to/tsan-binary" >&2
  exit 2
fi
binary=$1
log=$(mktemp)
trap 'rm -f "$log"' EXIT
set +e
TSAN_OPTIONS="halt_on_error=1:exitcode=66:${TSAN_OPTIONS:-}" "$binary" >"$log" 2>&1
status=$?
set -e
cat "$log"
if [[ $status -eq 0 ]]; then
  echo "TSAN PASS: instrumented audit finished without a reported race"
  exit 0
fi
if grep -Eq 'ThreadSanitizer: (unexpected memory mapping|failed to mmap|unable to mmap|unsupported)' "$log"; then
  echo "TSAN SKIP: sanitizer runtime cannot start in this environment; no race-free claim" >&2
  exit 77
fi
if [[ $status -eq 66 ]] || grep -Eq 'WARNING: ThreadSanitizer|data race' "$log"; then
  echo "TSAN FAIL: possible data race; investigate report" >&2
  exit 1
fi
echo "TSAN ERROR: nonzero exit $status; investigate runtime/test failure" >&2
exit 1
