#!/usr/bin/env python3
"""Paired compact-vs-cacheline-isolated benchmark runs; standard library only.

This is an experiment, not a claim that padding improves every workload.
"""
import argparse
import csv
import pathlib
import statistics
import subprocess

from traffic_profile_report import parse_output

MODES = ("compact", "padded")
PATTERNS = ("hot", "uniform", "skewed")


def sample(binary, pattern, costs, threads, requests, ips, seed):
    command = [str(binary), "lockfree", str(threads), str(requests),
               pattern, str(ips), costs, str(seed)]
    proc = subprocess.run(command, check=True, capture_output=True,
                          text=True, timeout=90)
    return parse_output(proc.stdout, "lockfree", pattern, costs,
                        threads, requests, ips, seed)


def paired_summary(pairs):
    if not pairs:
        raise ValueError("at least one matched pair is required")
    before = statistics.median(x["throughput"] for x, _ in pairs)
    after = statistics.median(y["throughput"] for _, y in pairs)
    ratios = [y["throughput"] / x["throughput"] for x, y in pairs]
    return {"compact_median_ops": round(before),
            "padded_median_ops": round(after),
            "median_paired_padded_over_compact": round(statistics.median(ratios), 3),
            "median_paired_percent_change": round(100 * (statistics.median(ratios) - 1), 1)}


def run_matrix(binaries, patterns, thread_counts, requests, ips, costs,
               repeats, warmups, output_dir):
    if repeats < 1 or warmups < 0 or not patterns or not thread_counts:
        raise ValueError("invalid experiment dimensions")
    raw = []
    summary = []
    for pattern in patterns:
        for threads in thread_counts:
            pairs = []
            for trial in range(-warmups, repeats):
                seed = 20270105 + max(0, trial)
                # Alternating order reduces the *systematic* warmup-order bias.
                order = MODES if trial % 2 == 0 else tuple(reversed(MODES))
                measurements = {}
                for mode in order:
                    outcome = sample(binaries[mode], pattern, costs,
                                     threads, requests, ips, seed)
                    measurements[mode] = outcome
                    if trial >= 0:
                        raw.append({"pattern": pattern, "costs": costs,
                                    "threads": threads, "requests": requests,
                                    "ips": ips, "repeat": trial + 1,
                                    "seed": seed, "layout": mode, **outcome})
                if trial >= 0:
                    pairs.append((measurements["compact"], measurements["padded"]))
            stats = paired_summary(pairs)
            row = {"pattern": pattern, "costs": costs, "threads": threads,
                   "requests": requests, "ips": ips, "repeats": repeats,
                   **stats}
            summary.append(row)
            print("pattern={pattern} threads={threads} "
                  "compact={compact_median_ops} padded={padded_median_ops} "
                  "paired_ratio={median_paired_padded_over_compact:.3f} "
                  "change={median_paired_percent_change:+.1f}%".format(**row))
    output_dir.mkdir(parents=True, exist_ok=True)
    for name, values in (("cacheline_raw.csv", raw),
                         ("cacheline_summary.csv", summary)):
        with (output_dir / name).open("w", newline="", encoding="utf-8") as f:
            writer = csv.DictWriter(f, fieldnames=list(values[0]))
            writer.writeheader()
            writer.writerows(values)
    return raw, summary


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--compact", default="./build/realistic_traffic_bench")
    p.add_argument("--padded", default="./build/realistic_traffic_bench_padded")
    p.add_argument("--patterns", default="uniform,skewed")
    p.add_argument("--threads", default="1,4,8")
    p.add_argument("--requests", type=int, default=60000)
    p.add_argument("--ips", type=int, default=64)
    p.add_argument("--costs", choices=("unit", "mixed"), default="unit")
    p.add_argument("--repeats", type=int, default=3)
    p.add_argument("--warmups", type=int, default=1)
    p.add_argument("--out", type=pathlib.Path, default=pathlib.Path("build"))
    args = p.parse_args()
    patterns = args.patterns.split(",")
    threads = [int(x) for x in args.threads.split(",")]
    if (not set(patterns) <= set(PATTERNS) or len(patterns) != len(set(patterns))
            or not threads or any(t < 1 or t > 16 for t in threads)
            or len(set(threads)) != len(threads) or args.requests < 1
            or args.requests > 10000000 or not 1 <= args.ips <= 1024
            or args.repeats < 1 or args.warmups < 0):
        p.error("invalid experiment parameters")
    run_matrix({"compact": args.compact, "padded": args.padded},
               patterns, threads, args.requests, args.ips, args.costs,
               args.repeats, args.warmups, args.out)


if __name__ == "__main__":
    main()
