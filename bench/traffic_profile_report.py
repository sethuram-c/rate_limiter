#!/usr/bin/env python3
"""Phase 21: validated paired A/B workload matrix; Python stdlib only.

Output has separate raw sample and median summaries. No universal speedup claims.
"""
import argparse
import csv
import math
import pathlib
import statistics
import subprocess

FIELDS = {"impl", "pattern", "costs", "threads", "requests", "ips", "seed",
          "allowed", "denied", "errors", "allowed_tokens", "elapsed_ns",
          "ops_per_sec", "valid"}
MODES = ("mutex", "lockfree")
PATTERNS = ("hot", "uniform", "skewed")
COSTS = ("unit", "mixed")


def parse_output(line, mode, pattern, costs, threads, requests, ips, seed):
    try:
        parts = [part.split("=", 1) for part in line.strip().split()]
        if len(parts) != len(FIELDS) or any(len(p) != 2 for p in parts):
            raise ValueError("malformed fields")
        values = dict(parts)
        if set(values) != FIELDS:
            raise ValueError("missing, repeated, or unexpected fields")
        if (values["impl"], values["pattern"], values["costs"]) != (mode, pattern, costs):
            raise ValueError("unexpected mode/pattern/cost")
        for key, expected in (("threads", threads), ("requests", requests),
                              ("ips", ips), ("seed", seed)):
            if int(values[key]) != expected:
                raise ValueError("unexpected benchmark parameter")
        allowed, denied, errors = [int(values[k]) for k in ("allowed", "denied", "errors")]
        tokens = int(values["allowed_tokens"])
        elapsed = int(values["elapsed_ns"])
        speed = float(values["ops_per_sec"])
        if (values["valid"] != "yes" or min(allowed, denied, errors, tokens) < 0
                or errors != 0 or allowed + denied != requests or elapsed <= 0
                or tokens < allowed or tokens > allowed * (3 if costs == "mixed" else 1)
                or not math.isfinite(speed) or speed <= 0
                or abs(speed - requests * 1e9 / elapsed) > 1.5):
            raise ValueError("accounting / duration / throughput mismatch")
    except (KeyError, TypeError, ValueError, OverflowError) as exc:
        raise ValueError(f"invalid benchmark record: {exc}") from exc
    return {"throughput": speed, "allowed": allowed, "denied": denied,
            "allowed_tokens": tokens, "elapsed_ns": elapsed}


def sample(binary, mode, pattern, costs, threads, requests, ips, seed):
    command = [str(binary), mode, str(threads), str(requests), pattern,
               str(ips), costs, str(seed)]
    proc = subprocess.run(command, capture_output=True, text=True,
                          check=True, timeout=90)
    return parse_output(proc.stdout, mode, pattern, costs,
                        threads, requests, ips, seed)


def median_pair(pairs):
    return (statistics.median(x["throughput"] for x, y in pairs),
            statistics.median(y["throughput"] for x, y in pairs),
            statistics.median(y["throughput"] / x["throughput"] for x, y in pairs))


def run_matrix(binary, thread_counts, requests, ips, repeats, warmups, output_dir):
    raw, summary = [], []
    for pattern in PATTERNS:
        for costs in COSTS:
            for threads in thread_counts:
                pairs = []
                for trial in range(-warmups, repeats):
                    # Different seeds across measured replicates, but a MATCHED
                    # seed for both implementations within each A/B pair.
                    seed = 20270104 + max(0, trial)
                    order = MODES if trial % 2 == 0 else tuple(reversed(MODES))
                    current = {}
                    for mode in order:
                        outcome = sample(binary, mode, pattern, costs, threads,
                                         requests, ips, seed)
                        current[mode] = outcome
                        if trial >= 0:
                            raw.append({"pattern": pattern, "costs": costs,
                                        "threads": threads, "repeat": trial + 1,
                                        "seed": seed, "impl": mode, **outcome})
                    if trial >= 0:
                        pairs.append((current["mutex"], current["lockfree"]))
                m, lf, ratio = median_pair(pairs)
                row = {"pattern": pattern, "costs": costs, "threads": threads,
                       "requests": requests, "ips": ips, "repeats": repeats,
                       "mutex_median_ops": round(m),
                       "lockfree_median_ops": round(lf),
                       "median_paired_lf_over_mutex": round(ratio, 3)}
                summary.append(row)
                print("pattern={pattern} costs={costs} threads={threads} "
                      "mutex_median_ops={mutex_median_ops} "
                      "lockfree_median_ops={lockfree_median_ops} "
                      "paired_lf_over_mutex={median_paired_lf_over_mutex:.3f}".format(**row))
    output_dir.mkdir(parents=True, exist_ok=True)
    for filename, rows in (("traffic_profile_raw.csv", raw),
                           ("traffic_profile_summary.csv", summary)):
        with (output_dir / filename).open("w", newline="", encoding="utf-8") as f:
            writer = csv.DictWriter(f, fieldnames=list(rows[0]))
            writer.writeheader()
            writer.writerows(rows)
    return raw, summary


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--benchmark", default="./build/realistic_traffic_bench")
    parser.add_argument("--threads", default="1,4,8")
    parser.add_argument("--requests", type=int, default=50000)
    parser.add_argument("--ips", type=int, default=64)
    parser.add_argument("--repeats", type=int, default=3)
    parser.add_argument("--warmups", type=int, default=1)
    parser.add_argument("--output-dir", default="build")
    opts = parser.parse_args()
    try:
        counts = [int(x) for x in opts.threads.split(",")]
    except ValueError:
        parser.error("thread values must be integers")
    if (not counts or any(n < 1 or n > 16 for n in counts)
            or not 1 <= opts.requests <= 10000000 or not 1 <= opts.ips <= 1024
            or opts.repeats < 1 or opts.warmups < 0):
        parser.error("out-of-range benchmark settings")
    run_matrix(pathlib.Path(opts.benchmark).resolve(), counts,
               opts.requests, opts.ips, opts.repeats,
               opts.warmups, pathlib.Path(opts.output_dir))


if __name__ == "__main__":
    main()
