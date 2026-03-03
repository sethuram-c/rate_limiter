#!/bin/sh
set -eu
bench=${1:-./build/mutex_compare}
for mode in lockfree mutex; do
  for spec in '1 1000 shared' '4 1000 shared' '4 1000 sharded' '8 120000 shared' '8 120000 sharded'; do
    # Intentional splitting of fixed internal test specifications.
    # shellcheck disable=SC2086
    output=$($bench "$mode" $spec)
    printf '%s\n' "$output"
    printf '%s\n' "$output" | grep -q 'valid=yes'
    printf '%s\n' "$output" | grep -q "impl=$mode"
  done
done
if "$bench" unknown 1 100 shared >/dev/null 2>&1; then exit 1; fi
if "$bench" mutex 0 100 shared >/dev/null 2>&1; then exit 1; fi
if "$bench" lockfree 1 0 shared >/dev/null 2>&1; then exit 1; fi
if "$bench" mutex 1 100 other >/dev/null 2>&1; then exit 1; fi
printf 'mutex benchmark smoke tests passed\n'
