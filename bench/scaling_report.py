#!/usr/bin/env python3
"""Repeat Phase 18 fixed-clock microbenchmarks and summarize thread scaling.

Python standard library only. Results describe a *particular machine/run*, not
network throughput and not a mutex/lock-free performance comparison.
"""
from __future__ import annotations

import argparse
import csv
import math
from pathlib import Path
import statistics
import subprocess
import sys

RAW_FIELDS = ("scenario", "threads", "run", "requests", "elapsed_ns", "ops_per_sec",
              "allowed", "denied", "errors")
SUMMARY_FIELDS = ("scenario", "threads", "repeats", "requests", "median_ops_per_sec",
                  "p10_ops_per_sec", "p90_ops_per_sec", "min_ops_per_sec",
                  "max_ops_per_sec", "mad_ops_per_sec", "speedup", "efficiency")


def percentile(values: list[float], quantile: float) -> float:
    """Linearly interpolate in sorted data at (n-1)*quantile; not a latency p99."""
    if not values or not 0.0 <= quantile <= 1.0:
        raise ValueError("percentile requires data and quantile in [0, 1]")
    ordered = sorted(values)
    position = (len(ordered) - 1) * quantile
    lo = math.floor(position)
    hi = math.ceil(position)
    return ordered[lo] + (ordered[hi] - ordered[lo]) * (position - lo)


def parse_benchmark(output: str, scenario: str, threads: int, requests: int) -> dict:
    """Reject malformed, unsuccessful, or arithmetically inconsistent benchmark output."""
    lines = output.strip().splitlines()
    if len(lines) != 1:
        raise ValueError("benchmark must emit exactly one result line")
    values = {}
    for part in lines[0].split():
        if part.count("=") != 1:
            raise ValueError(f"malformed benchmark field: {part!r}")
        key, value = part.split("=", 1)
        if key in values:
            raise ValueError(f"duplicate benchmark field: {key}")
        values[key] = value
    required = ("scenario", "threads", "requests", "allowed", "denied", "errors",
                "elapsed_ns", "ops_per_sec", "valid")
    if any(key not in values for key in required):
        raise ValueError("missing required benchmark field")
    if values["scenario"] != scenario or values["valid"] != "yes":
        raise ValueError("scenario mismatch or invalid benchmark result")
    try:
        count_threads = int(values["threads"])
        count_requests = int(values["requests"])
        allowed, denied, errors = (int(values[key]) for key in ("allowed", "denied", "errors"))
        elapsed_ns = int(values["elapsed_ns"])
        displayed_rate = float(values["ops_per_sec"])
    except (ValueError, OverflowError) as exc:
        raise ValueError("invalid numeric benchmark field") from exc
    if (count_threads != threads or count_requests != requests or
            min(allowed, denied, errors) < 0 or errors != 0 or
            allowed + denied + errors != requests or elapsed_ns <= 0 or
            not math.isfinite(displayed_rate) or displayed_rate < 0):
        raise ValueError("inconsistent benchmark accounting or timing")
    # Recompute from unrounded elapsed time rather than the printed ops_per_sec.
    rate = requests * 1_000_000_000.0 / elapsed_ns
    if not math.isfinite(rate) or rate <= 0:
        raise ValueError("invalid calculated throughput")
    return dict(scenario=scenario, threads=threads, requests=requests,
                elapsed_ns=elapsed_ns, ops_per_sec=rate, allowed=allowed,
                denied=denied, errors=errors)


def run_once(benchmark: Path, scenario: str, threads: int, requests: int) -> dict:
    command = [str(benchmark), str(threads), str(requests), scenario]
    try:
        result = subprocess.run(command, text=True, capture_output=True,
                                check=False, timeout=120)
    except (OSError, subprocess.TimeoutExpired) as exc:
        raise RuntimeError(f"cannot execute {command!r}: {exc}") from exc
    if result.returncode != 0:
        raise RuntimeError(f"benchmark failed (exit {result.returncode}): {result.stderr.strip()}")
    return parse_benchmark(result.stdout, scenario, threads, requests)


def summarize(samples: list[dict], threads: list[int], scenarios: list[str],
              requests: int) -> list[dict]:
    results = []
    for scenario in scenarios:
        grouped = {t: [s["ops_per_sec"] for s in samples
                       if s["scenario"] == scenario and s["threads"] == t]
                   for t in threads}
        if not grouped[1] or any(not rows for rows in grouped.values()):
            raise ValueError("missing baseline or scenario samples")
        baseline = statistics.median(grouped[1])
        for t in threads:
            rates = grouped[t]
            median = statistics.median(rates)
            mad = statistics.median([abs(r - median) for r in rates])
            speedup = median / baseline
            results.append(dict(scenario=scenario, threads=t, repeats=len(rates),
                                requests=requests, median_ops_per_sec=median,
                                p10_ops_per_sec=percentile(rates, 0.10),
                                p90_ops_per_sec=percentile(rates, 0.90),
                                min_ops_per_sec=min(rates), max_ops_per_sec=max(rates),
                                mad_ops_per_sec=mad, speedup=speedup,
                                efficiency=speedup / t))
    return results


def write_csv(path: Path, fields: tuple[str, ...], rows: list[dict]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("w", newline="", encoding="utf-8") as file:
        writer = csv.DictWriter(file, fieldnames=fields)
        writer.writeheader()
        writer.writerows(rows)


def thread_list(value: str) -> list[int]:
    try:
        numbers = [int(x) for x in value.split(",")]
    except ValueError as exc:
        raise argparse.ArgumentTypeError("threads must be comma-separated integers") from exc
    if (not numbers or 1 not in numbers or len(numbers) != len(set(numbers)) or
            any(t < 1 or t > 16 for t in numbers)):
        raise argparse.ArgumentTypeError("threads must be unique, include 1, and be in 1..16")
    return sorted(numbers)


def positive(value: str) -> int:
    num = int(value)
    if num <= 0:
        raise argparse.ArgumentTypeError("value must be positive")
    return num


def nonnegative(value: str) -> int:
    num = int(value)
    if num < 0:
        raise argparse.ArgumentTypeError("value cannot be negative")
    return num


def argument_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description="Repeated in-process throughput scaling report")
    parser.add_argument("--benchmark", type=Path, default=Path("build/workload_bench"))
    parser.add_argument("--threads", type=thread_list, default=[1, 2, 4, 8])
    parser.add_argument("--requests", type=positive, default=200_000)
    parser.add_argument("--repeats", type=positive, default=5)
    parser.add_argument("--warmups", type=nonnegative, default=1)
    parser.add_argument("--scenario", choices=("both", "shared", "sharded"), default="both")
    parser.add_argument("--raw-csv", type=Path, default=Path("build/scaling_raw.csv"))
    parser.add_argument("--summary-csv", type=Path, default=Path("build/scaling_summary.csv"))
    return parser


def main(argv: list[str] | None = None) -> int:
    parser = argument_parser()
    args = parser.parse_args(argv)
    if args.requests > 10_000_000 or args.repeats > 50 or args.warmups > 10:
        parser.error("requests <= 10000000, repeats <= 50, warmups <= 10 required")
    binary = args.benchmark.resolve()
    if not binary.is_file():
        parser.error(f"benchmark not found: {binary}; first run make workload-bench")
    scenarios = ["shared", "sharded"] if args.scenario == "both" else [args.scenario]
    samples = []
    for scenario in scenarios:
        # Cycle over configurations instead of running every sample for one thread count.
        for round_number in range(args.warmups + args.repeats):
            shift = round_number % len(args.threads)
            order = args.threads[shift:] + args.threads[:shift]
            for threads in order:
                try:
                    record = run_once(binary, scenario, threads, args.requests)
                except (RuntimeError, ValueError) as exc:
                    print(f"ERROR: {scenario} threads={threads}: {exc}", file=sys.stderr)
                    return 1
                if round_number >= args.warmups:
                    record["run"] = round_number - args.warmups + 1
                    samples.append(record)
    summary = summarize(samples, args.threads, scenarios, args.requests)
    write_csv(args.raw_csv, RAW_FIELDS, samples)
    write_csv(args.summary_csv, SUMMARY_FIELDS, summary)
    print(f"Collected {len(samples)} measured runs (+{len(scenarios) * len(args.threads) * args.warmups} warmups); fixed {args.requests:,} total requests per run")
    print(f"{'mode':<8} {'thr':>3} {'median Mops/s':>14} {'MAD Mops/s':>12} {'speedup':>8} {'efficiency':>10}")
    for row in summary:
        print(f"{row['scenario']:<8} {row['threads']:>3} "
              f"{row['median_ops_per_sec']/1e6:>14.3f} "
              f"{row['mad_ops_per_sec']/1e6:>12.3f} "
              f"{row['speedup']:>8.2f} {row['efficiency']:>10.2%}")
    print(f"Raw samples: {args.raw_csv}; summary: {args.summary_csv}")
    print("NOTE: in-process throughput incl. pthread startup/join; no HTTP, no mutex baseline, no per-request p99 latency")
    return 0


if __name__ == "__main__":
    sys.exit(main())
