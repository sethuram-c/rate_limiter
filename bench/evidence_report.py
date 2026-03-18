#!/usr/bin/env python3
"""Phase 35: fail-closed, paired lock-free versus mutex benchmark evidence.

Uses the Phase 20 benchmark rather than introducing a new timed hot path.
Generated reports belong under ignored build/, never in the source repository.
"""
import argparse
import csv
from datetime import datetime, timezone
import hashlib
import json
import os
from pathlib import Path
import platform
import statistics
import subprocess
import sys
import tempfile

from mutex_compare_report import sample

SCENARIOS = ("shared", "sharded")
MODES = ("mutex", "lockfree")
RAW_FIELDS = ("scenario", "threads", "stage", "pair", "order", "impl", "requests",
              "allowed", "denied", "errors", "elapsed_ns", "ops_per_sec")
SUMMARY_FIELDS = ("scenario", "threads", "measured_pairs", "mutex_median_ops",
                  "lockfree_median_ops", "paired_ratio_median", "paired_ratio_mad",
                  "min_paired_ratio", "max_paired_ratio", "observed_direction")


def parse_threads(value):
    try:
        counts = [int(piece) for piece in value.split(",")]
    except ValueError as exc:
        raise ValueError("--threads must be comma-separated integers") from exc
    if not counts or any(t < 1 or t > 16 for t in counts) or len(set(counts)) != len(counts):
        raise ValueError("thread counts must be unique integers in 1..16")
    return counts


def order_for_pair(pair_index, scenario_index, thread_index):
    """Alternate the first implementation; keep warmups and measured runs balanced."""
    return MODES if (pair_index + scenario_index + thread_index) % 2 == 0 else MODES[::-1]


def summarise_pair_results(pairs, scenario, threads):
    """Only valid, matching observed admissions are accepted in a pair."""
    if not pairs:
        raise ValueError("at least one measured pair is required")
    ratios, mutex_rates, lockfree_rates = [], [], []
    for mutex, lockfree in pairs:
        if (mutex["allowed"], mutex["denied"]) != (lockfree["allowed"], lockfree["denied"]):
            raise ValueError("implementations have unequal admission accounting")
        if mutex["throughput"] <= 0 or lockfree["throughput"] <= 0:
            raise ValueError("non-positive throughput")
        mutex_rates.append(mutex["throughput"])
        lockfree_rates.append(lockfree["throughput"])
        ratios.append(lockfree["throughput"] / mutex["throughput"])
    middle = statistics.median(ratios)
    mad = statistics.median(abs(r - middle) for r in ratios)
    direction = ("all-pairs-LF-higher" if min(ratios) > 1 else
                 "all-pairs-mutex-higher" if max(ratios) < 1 else "mixed-or-equal")
    return {
        "scenario": scenario, "threads": threads, "measured_pairs": len(pairs),
        "mutex_median_ops": round(statistics.median(mutex_rates)),
        "lockfree_median_ops": round(statistics.median(lockfree_rates)),
        "paired_ratio_median": round(middle, 5),
        "paired_ratio_mad": round(mad, 5),
        "min_paired_ratio": round(min(ratios), 5),
        "max_paired_ratio": round(max(ratios), 5),
        "observed_direction": direction,
    }


def collect(benchmark, threads, requests, repeats, warmups, runner=sample):
    """Collect everything in memory; no report is emitted if any run fails."""
    raw, summary = [], []
    for scenario_index, scenario in enumerate(SCENARIOS):
        for thread_index, thread_count in enumerate(threads):
            measured = []
            for pair_index in range(-warmups, repeats):
                stage = "warmup" if pair_index < 0 else "measure"
                order = order_for_pair(pair_index, scenario_index, thread_index)
                pair = {}
                for sequence, mode in enumerate(order, start=1):
                    result = runner(benchmark, mode, scenario, thread_count, requests)
                    if any(key not in result for key in ("allowed", "denied", "elapsed_ns", "throughput")):
                        raise ValueError("benchmark returned incomplete data")
                    if result["allowed"] + result["denied"] != requests:
                        raise ValueError("request accounting mismatch")
                    if result["elapsed_ns"] <= 0 or result["throughput"] <= 0:
                        raise ValueError("invalid benchmark duration or throughput")
                    pair[mode] = result
                    raw.append({"scenario": scenario, "threads": thread_count,
                                "stage": stage, "pair": pair_index + 1 if pair_index >= 0 else pair_index,
                                "order": sequence, "impl": mode, "requests": requests,
                                "allowed": result["allowed"], "denied": result["denied"],
                                "errors": 0, "elapsed_ns": result["elapsed_ns"],
                                "ops_per_sec": result["throughput"]})
                if (pair["mutex"]["allowed"], pair["mutex"]["denied"]) != (
                    pair["lockfree"]["allowed"], pair["lockfree"]["denied"]):
                    raise ValueError("paired implementations disagree on quota conservation")
                if stage == "measure":
                    measured.append((pair["mutex"], pair["lockfree"]))
            summary.append(summarise_pair_results(measured, scenario, thread_count))
    return raw, summary


def cpu_model():
    try:
        with open("/proc/cpuinfo", encoding="utf-8") as source:
            for line in source:
                if line.lower().startswith("model name"):
                    return line.split(":", 1)[1].strip()
    except OSError:
        pass
    return "unavailable"


def git_revision(repo_dir):
    try:
        return subprocess.run(["git", "-C", str(repo_dir), "rev-parse", "HEAD"],
                              capture_output=True, text=True, timeout=3,
                              check=True).stdout.strip()
    except (OSError, subprocess.CalledProcessError, subprocess.TimeoutExpired):
        return "unavailable (source-only ZIP has no .git)"


def metadata(benchmark, threads, requests, repeats, warmups, compiler_note):
    return {
        "schema": "phase35-evidence-v1", "created_utc": datetime.now(timezone.utc).isoformat(),
        "source_revision": git_revision(Path(__file__).resolve().parents[1]),
        "benchmark_binary": str(benchmark.resolve()),
        "benchmark_sha256": hashlib.sha256(benchmark.read_bytes()).hexdigest(),
        "platform": platform.system(), "kernel": platform.release(),
        "architecture": platform.machine(), "logical_cpu_count": os.cpu_count(),
        "cpu_model": cpu_model(), "python_version": platform.python_version(),
        "compiler_note_user_supplied": compiler_note,
        "scenarios": list(SCENARIOS), "threads": threads, "requests_per_run": requests,
        "measured_pairs": repeats, "warmup_pairs": warmups,
        "timed_region": "worker gate release through pthread joins; thread creation excluded",
        "benchmark_clock": "CLOCK_MONOTONIC for elapsed; fixed simulated time for bucket refill",
        "limitations": ["in-process synthetic benchmark, not HTTP throughput",
                        "no refill in timed request path", "denied operations still counted",
                        "median ratios are descriptive, not confidence intervals",
                        "possible CPU scheduler and thermal variation"],
    }


def render_markdown(meta, summary):
    safe_cpu = meta["cpu_model"].replace("|", "/").replace("\n", " ")
    lines = ["# Phase 35 - Reproducible benchmark evidence (local run)", "",
             "This file is GENERATED from a local experiment. Do not commit it as a",
             "universal performance claim. The source repository tracks the method,",
             "not machine-dependent measured results.", "",
             "## Reproduction metadata", "",
             f"- UTC: `{meta['created_utc']}`",
             f"- Source Git SHA: `{meta['source_revision']}`",
             f"- Benchmark SHA-256: `{meta['benchmark_sha256']}`",
             f"- OS: `{meta['platform']} {meta['kernel']}`; arch `{meta['architecture']}`",
             f"- CPU: {safe_cpu}; logical CPUs: {meta['logical_cpu_count']}",
             f"- Compiler/build note (supplied): `{meta['compiler_note_user_supplied']}`",
             f"- Requests per run: {meta['requests_per_run']:,}; "
             f"warmup pairs: {meta['warmup_pairs']}; measured pairs: {meta['measured_pairs']}", "",
             "## Paired results", "",
             "| Scenario | Threads | Mutex median req/s | CAS median req/s | "
             "Paired median CAS/mutex | Ratio MAD | Observed direction |",
             "|---|---:|---:|---:|---:|---:|---|"]
    for row in summary:
        lines.append(f"| {row['scenario']} | {row['threads']} | {row['mutex_median_ops']:,} | "
                     f"{row['lockfree_median_ops']:,} | {row['paired_ratio_median']:.3f} | "
                     f"{row['paired_ratio_mad']:.3f} | {row['observed_direction']} |")
    lines += ["", "A ratio greater than 1 means the lock-free path was faster in the",
              "paired samples **for this configuration**. A ratio less than 1 favors",
              "the mutex baseline. Mixed/equal ratios show no consistent ordering.",
              "Even unanimous samples do not establish statistical significance.", "",
              "## Timing and validity", "",
              "- Both implementations use the same atomic IP index and state transitions.",
              "- Every run checks `valid=yes`, exact admission totals and zero errors.",
              "- Per-pair order rotates; warmups are excluded from summaries.",
              "- The clock used by token buckets is fixed, so no refill occurs.",
              "- The timed region includes scheduling and worker joins, but excludes",
              "  thread creation, initialization and key prepopulation.",
              "- These results cannot establish HTTP/network-service throughput or",
              "  unconditional lock-free superiority.", "",
              "## Files", "",
              "`evidence_raw.csv` records every run, including warmups;",
              "`evidence_summary.csv` contains per-configuration descriptive statistics;",
              "`evidence_environment.json` captures metadata and benchmark binary hash.", ""]
    return "\n".join(lines)


def write_csv(path, fields, records):
    with path.open("w", newline="", encoding="utf-8") as stream:
        writer = csv.DictWriter(stream, fieldnames=fields, extrasaction="raise")
        writer.writeheader()
        writer.writerows(records)


def persist_reports(directory, raw, summary, meta):
    directory.mkdir(parents=True, exist_ok=True)
    # Only write after all sampled commands are successful; stage each file.
    with tempfile.TemporaryDirectory(prefix=".evidence-stage-", dir=directory) as folder:
        staged = Path(folder)
        write_csv(staged / "evidence_raw.csv", RAW_FIELDS, raw)
        write_csv(staged / "evidence_summary.csv", SUMMARY_FIELDS, summary)
        (staged / "evidence_environment.json").write_text(
            json.dumps(meta, indent=2, sort_keys=True) + "\n", encoding="utf-8")
        (staged / "evidence_report.md").write_text(
            render_markdown(meta, summary), encoding="utf-8")
        for name in ("evidence_raw.csv", "evidence_summary.csv",
                     "evidence_environment.json", "evidence_report.md"):
            (staged / name).replace(directory / name)


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--benchmark", default="./build/mutex_compare")
    parser.add_argument("--threads", default="1,2,4,8")
    parser.add_argument("--requests", type=int, default=100000)
    parser.add_argument("--repeats", type=int, default=5)
    parser.add_argument("--warmups", type=int, default=1)
    parser.add_argument("--output-dir", default="build")
    parser.add_argument("--compiler-note", default="unspecified")
    args = parser.parse_args(argv)
    try:
        counts = parse_threads(args.threads)
    except ValueError as exc:
        parser.error(str(exc))
    if not (1 <= args.requests <= 10000000):
        parser.error("requests must be in 1..10000000")
    if not (1 <= args.repeats <= 100) or not (0 <= args.warmups <= 20):
        parser.error("repeats must be 1..100 and warmups 0..20")
    benchmark = Path(args.benchmark).resolve()
    if not benchmark.is_file() or not os.access(benchmark, os.X_OK):
        parser.error("benchmark executable not found or not executable; run make mutex-compare-test")
    try:
        raw, summary = collect(benchmark, counts, args.requests, args.repeats, args.warmups)
        meta = metadata(benchmark, counts, args.requests, args.repeats,
                        args.warmups, args.compiler_note)
        persist_reports(Path(args.output_dir), raw, summary, meta)
    except (OSError, ValueError, subprocess.SubprocessError) as exc:
        print(f"BENCHMARK EVIDENCE FAILED: {exc}", file=sys.stderr)
        return 1
    for row in summary:
        print(f"{row['scenario']} threads={row['threads']} ratio={row['paired_ratio_median']:.3f} "
              f"MAD={row['paired_ratio_mad']:.3f} {row['observed_direction']}")
    print(f"Evidence saved to {args.output_dir}/evidence_*.csv/json/md (not tracked by Git)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
