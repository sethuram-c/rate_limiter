#!/bin/sh
set -eu
bench=${1:-./build/workload_bench}
for spec in '1 1000 shared' '4 1000 shared' '4 1000 sharded' '8 200000 shared' '8 200000 sharded'; do
  # shell splitting of trusted fixed specs is intentional.
  # shellcheck disable=SC2086
  output=$($bench $spec)
  printf '%s\n' "$output"
  printf '%s\n' "$output" | grep -q 'valid=yes'
done
if "$bench" 0 100 shared >/dev/null 2>&1; then exit 1; fi
if "$bench" 1 0 shared >/dev/null 2>&1; then exit 1; fi
if "$bench" 1 100 invalid >/dev/null 2>&1; then exit 1; fi
printf 'benchmark smoke tests passed\n'
