"""Phase 35: exercise runner validity, summary arithmetic, report contracts."""
import csv
import json
import pathlib
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1] / "bench"))
import evidence_report as evidence


class EvidenceTests(unittest.TestCase):
    def result(self, benchmark, mode, scenario, threads, requests):
        allowed = min(requests, 100)
        return {"allowed": allowed, "denied": requests - allowed,
                "elapsed_ns": 5000 if mode == "mutex" else 4000,
                "throughput": 10_000_000 if mode == "mutex" else 12_500_000}

    def test_thread_parser_rejects_empty_duplicate_and_out_of_range(self):
        for value in ("", "1,1", "0", "17", "1,foo", "1,"):
            with self.subTest(value=value), self.assertRaises(ValueError):
                evidence.parse_threads(value)
        self.assertEqual(evidence.parse_threads("1,2,8"), [1, 2, 8])

    def test_pair_order_alternates(self):
        first = evidence.order_for_pair(0, 0, 0)
        self.assertEqual(first, ("mutex", "lockfree"))
        self.assertEqual(evidence.order_for_pair(1, 0, 0), first[::-1])
        self.assertEqual(evidence.order_for_pair(0, 1, 0), first[::-1])

    def test_median_mad_and_direction(self):
        def r(speed):
            return {"allowed": 8, "denied": 2, "throughput": speed}
        samples = [(r(10), r(20)), (r(10), r(15)), (r(10), r(30))]
        row = evidence.summarise_pair_results(samples, "shared", 4)
        self.assertEqual(row["paired_ratio_median"], 2)
        self.assertEqual(row["paired_ratio_mad"], 0.5)
        self.assertEqual(row["observed_direction"], "all-pairs-LF-higher")

    def test_mixed_sample_direction(self):
        a = {"allowed": 1, "denied": 0, "throughput": 10}
        b = {"allowed": 1, "denied": 0, "throughput": 20}
        self.assertEqual(evidence.summarise_pair_results([(a, b), (b, a)], "shared", 1)
                         ["observed_direction"], "mixed-or-equal")

    def test_mismatched_admissions_fail_closed(self):
        first = {"allowed": 4, "denied": 6, "throughput": 10}
        second = {"allowed": 5, "denied": 5, "throughput": 12}
        with self.assertRaises(ValueError):
            evidence.summarise_pair_results([(first, second)], "shared", 2)

    def test_collect_keeps_warmups_out_of_summary(self):
        raw, summary = evidence.collect("ignored", [2], 500, 2, 1, self.result)
        self.assertEqual(len(raw), 12)
        self.assertEqual(len(summary), 2)
        self.assertEqual(len([r for r in raw if r["stage"] == "warmup"]), 4)
        self.assertTrue(all(r["measured_pairs"] == 2 for r in summary))

    def test_failure_does_not_create_reports(self):
        def broken(*_):
            raise ValueError("intentional bad run")
        with tempfile.TemporaryDirectory() as folder:
            dest = pathlib.Path(folder) / "evidence"
            with self.assertRaisesRegex(ValueError, "intentional"):
                raw, summary = evidence.collect("ignored", [1], 5, 1, 0, broken)
                evidence.persist_reports(dest, raw, summary, {})
            self.assertFalse(dest.exists())

    def test_generated_markdown_has_caveats(self):
        meta = {"created_utc": "example", "source_revision": "none",
                "benchmark_sha256": "aa", "platform": "Linux", "kernel": "test",
                "architecture": "x86_64", "cpu_model": "test", "logical_cpu_count": 2,
                "compiler_note_user_supplied": "unspecified", "requests_per_run": 10,
                "warmup_pairs": 1, "measured_pairs": 3}
        text = evidence.render_markdown(meta, [{"scenario": "shared", "threads": 1,
               "mutex_median_ops": 1000, "lockfree_median_ops": 1100,
               "paired_ratio_median": 1.1, "paired_ratio_mad": 0.1,
               "observed_direction": "mixed-or-equal"}])
        self.assertIn("universal performance claim", text)
        self.assertIn("not establish HTTP", text)

    def test_real_cli_creates_valid_evidence(self):
        benchmark = pathlib.Path("build/mutex_compare").resolve()
        with tempfile.TemporaryDirectory() as folder:
            dest = pathlib.Path(folder) / "evidence"
            proc = subprocess.run([sys.executable, "bench/evidence_report.py", "--benchmark",
                                   str(benchmark), "--threads", "1,2", "--requests", "400",
                                   "--repeats", "2", "--warmups", "1",
                                   "--output-dir", str(dest)], capture_output=True,
                                  text=True, timeout=50)
            self.assertEqual(proc.returncode, 0, proc.stderr)
            with (dest / "evidence_raw.csv").open(newline="") as source:
                raw = list(csv.DictReader(source))
            with (dest / "evidence_summary.csv").open(newline="") as source:
                summary = list(csv.DictReader(source))
            meta = json.loads((dest / "evidence_environment.json").read_text())
            self.assertEqual(len(raw), 24)
            self.assertEqual(len(summary), 4)
            self.assertEqual(meta["schema"], "phase35-evidence-v1")
            self.assertEqual(len(meta["benchmark_sha256"]), 64)
            self.assertIn("Paired results", (dest / "evidence_report.md").read_text())


if __name__ == "__main__":
    unittest.main()
