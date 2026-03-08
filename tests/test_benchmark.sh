#!/usr/bin/env bash
set -euo pipefail
benchmark=${1:-./build/benchmark_rate_service}

run_check() {
  local pattern=$1 threads=$2 requests=$3 ips=$4 capacity=$5
  local output
  output=$("$benchmark" --pattern "$pattern" --threads "$threads" \
    --requests "$requests" --ips "$ips" --capacity "$capacity" \
    --seed 123456 --clock fixed --csv)
  printf '%s\n' "$output" | awk -F, \
    -v t="$threads" -v n="$requests" -v p="$pattern" -v ips="$ips" \
    -v cap="$capacity" '
      NR==1 { if ($1!="pattern" || $8!="total" || $9!="allowed") exit 1 }
      NR==2 {
        if ($1!=p || $2!="fixed" || $3!=t || $4!=n || $5!=ips || $7!=cap) exit 1;
        if ($8!=t*n || $9+$10!=$8 || $11<=0 || $12<=0) exit 1;
        if (p=="hot" && $9!=cap) exit 1;
        rows++;
      }
      END { if (NR!=2 || rows!=1) exit 1 }
    '
}
run_check hot 4 1000 32 7
run_check uniform 4 1000 32 7
run_check skewed 4 1000 32 7
run_check hot 1 10 1 3
if "$benchmark" --threads 0 >/dev/null 2>&1; then
  echo 'invalid thread count was accepted' >&2; exit 1
fi
if "$benchmark" --ips 65537 >/dev/null 2>&1; then
  echo 'invalid IP count was accepted' >&2; exit 1
fi
if "$benchmark" --pattern typo >/dev/null 2>&1; then
  echo 'invalid pattern was accepted' >&2; exit 1
fi
if "$benchmark" --requests -1 >/dev/null 2>&1; then
  echo 'negative count was accepted' >&2; exit 1
fi
"$benchmark" --pattern uniform --threads 2 --requests 1000 \
  --ips 16 --clock monotonic --csv | awk -F, '
  NR==2 {if ($2!="monotonic" || $8!=2000 || $9+$10!=2000) exit 1; seen=1}
  END { if (!seen || NR!=2) exit 1 }
'
echo 'benchmark CLI tests passed: deterministic workloads, invalid flags, monotonic clock'
