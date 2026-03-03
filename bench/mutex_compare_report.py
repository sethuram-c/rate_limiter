#!/usr/bin/env python3
"""Phase 20: paired, order-rotated mutex/CAS benchmark; Python stdlib only.

The results are samples of this environment, NOT guaranteed superiority.
Output CSVs go to build/ and stay outside version control.
"""
import argparse
import csv
import math
import pathlib
import statistics
import subprocess

MODES = ("mutex", "lockfree")
FIELDS = ("impl", "scenario", "threads", "requests", "allowed", "denied",
          "errors", "elapsed_ns", "ops_per_sec", "valid")


def parse_line(line, mode, scenario, threads, requests):
    values = dict(part.split("=", 1) for part in line.strip().split())
    if set(values) != set(FIELDS):
        raise ValueError("missing/unexpected benchmark fields")
    if values["impl"] != mode or values["scenario"] != scenario:
        raise ValueError("incorrect implementation or scenario")
    if int(values["threads"]) != threads or int(values["requests"]) != requests:
        raise ValueError("incorrect thread or request count")
    allowed, denied, errors = (int(values[key]) for key in ("allowed", "denied", "errors"))
    elapsed = int(values["elapsed_ns"])
    speed = float(values["ops_per_sec"])
    if (values["valid"] != "yes" or min(allowed, denied, errors) < 0
            or errors or allowed + denied != requests or elapsed <= 0
            or not math.isfinite(speed) or speed <= 0
            or abs(speed - requests * 1000000000 / elapsed) > 1.5):
        raise ValueError("invalid run/accounting")
    return {"throughput": speed, "elapsed_ns": elapsed,
            "allowed": allowed, "denied": denied}


def sample(executable, mode, scenario, threads, requests):
    p = subprocess.run([str(executable), mode, str(threads), str(requests), scenario],
                       check=True, capture_output=True, text=True, timeout=90)
    return parse_line(p.stdout, mode, scenario, threads, requests)


def compare(samples):
    """Return per-mode medians and paired median throughput ratio (LF / mutex)."""
    m = [a["throughput"] for a, b in samples]
    lf = [b["throughput"] for a, b in samples]
    return (statistics.median(m), statistics.median(lf),
            statistics.median(b / a for a, b in zip(m, lf)))


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--benchmark", default="./build/mutex_compare")
    p.add_argument("--threads", default="1,2,4,8")
    p.add_argument("--requests", type=int, default=200000)
    p.add_argument("--repeats", type=int, default=5)
    p.add_argument("--warmups", type=int, default=1)
    p.add_argument("--output-dir", default="build")
    a = p.parse_args()
    thread_counts = [int(x) for x in a.threads.split(",")]
    if not thread_counts or any(t < 1 or t > 16 for t in thread_counts):
        p.error("thread counts must be 1..16")
    if a.requests < 1 or a.requests > 10000000 or a.repeats < 1 or a.warmups < 0:
        p.error("invalid requests/repeats/warmups")
    folder = pathlib.Path(a.output_dir)
    folder.mkdir(parents=True, exist_ok=True)
    raw, summary = [], []
    for scenario in ("shared", "sharded"):
        for threads in thread_counts:
            measured = []
            for iteration in range(-a.warmups, a.repeats):
                # AB/BA: reverse mode order every repetition to reduce drift bias.
                order = MODES if iteration % 2 == 0 else tuple(reversed(MODES))
                pair = {}
                for mode in order:
                    pair[mode] = sample(a.benchmark, mode, scenario, threads, a.requests)
                    if iteration >= 0:
                        result = pair[mode]
                        raw.append({"scenario": scenario, "threads": threads,
                                    "repeat": iteration + 1, "mode": mode,
                                    **result})
                if iteration >= 0:
                    measured.append((pair["mutex"], pair["lockfree"]))
            mutex_med, lf_med, paired_ratio = compare(measured)
            row = {"scenario": scenario, "threads": threads,
                   "repeats": a.repeats, "mutex_median_ops": round(mutex_med),
                   "lockfree_median_ops": round(lf_med),
                   "median_paired_lf_over_mutex": round(paired_ratio, 3)}
            summary.append(row)
            print("scenario={scenario} threads={threads} mutex_median_ops={mutex_median_ops} "
                  "lockfree_median_ops={lockfree_median_ops} "
                  "paired_lf_over_mutex={median_paired_lf_over_mutex:.3f}".format(**row))
    for name, rows in (("mutex_comparison_raw.csv", raw),
                       ("mutex_comparison_summary.csv", summary)):
        with (folder / name).open("w", newline="", encoding="utf-8") as stream:
            w = csv.DictWriter(stream, fieldnames=list(rows[0]))
            w.writeheader()
            w.writerows(rows)
    print("wrote", folder / "mutex_comparison_raw.csv", "and", folder / "mutex_comparison_summary.csv")


if __name__ == "__main__":
    main()
