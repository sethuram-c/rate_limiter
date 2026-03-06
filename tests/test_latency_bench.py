#!/usr/bin/env python3
"""Run both implementations, verify monotonic ordered percentile output."""
import re
import subprocess
import sys

binary = sys.argv[1]
for implementation in ("mutex", "lockfree"):
    for scenario in ("shared", "sharded"):
        output = subprocess.check_output([binary, implementation, "4", "2000", scenario], text=True)
        assert "valid=yes" in output, output
        m = re.search(r"latency_samples=(\d+) p50_ns=(\d+) p95_ns=(\d+) p99_ns=(\d+) max_ns=(\d+)", output)
        assert m, output
        n, p50, p95, p99, maximum = map(int, m.groups())
        assert n == 2000
        assert 0 <= p50 <= p95 <= p99 <= maximum
        print(f"{implementation} {scenario}: N={n}, p50={p50}ns p95={p95}ns p99={p99}ns")
print("latency benchmark tests passed")
