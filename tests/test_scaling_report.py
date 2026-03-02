"""Phase 19 validation: robust statistics and faithful parsing of real benchmark output."""
import csv
import io
from contextlib import redirect_stdout
from pathlib import Path
import sys
import tempfile
import unittest
from unittest import mock

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "bench"))
import scaling_report as scaling  # noqa: E402


class ScalingTests(unittest.TestCase):
    def test_percentile_math(self):
        self.assertEqual(scaling.percentile([1, 3, 5], 0.5), 3)
        self.assertEqual(scaling.percentile([10, 20], 0.1), 11)
        self.assertEqual(scaling.percentile([10], 0.9), 10)
        with self.assertRaises(ValueError):
            scaling.percentile([], 0.5)

    def test_parsing_valid_and_invalid(self):
        line = ("scenario=shared threads=4 requests=100 allowed=37 "
                "denied=63 errors=0 elapsed_ns=100000 ops_per_sec=1000000 valid=yes")
        row = scaling.parse_benchmark(line, "shared", 4, 100)
        self.assertEqual(row["ops_per_sec"], 1_000_000)
        for corrupted in (line.replace("valid=yes", "valid=no"),
                          line.replace("allowed=37", "allowed=38"),
                          line.replace("errors=0", "errors=1"),
                          line.replace("elapsed_ns=100000", "elapsed_ns=0"),
                          line.replace("shared", "sharded"),
                          line + "\nsecond line",
                          line.replace("denied=63 ", "")):
            with self.subTest(corrupted=corrupted):
                with self.assertRaises(ValueError):
                    scaling.parse_benchmark(corrupted, "shared", 4, 100)

    def test_statistics_speedup_and_mad(self):
        rates = [(1, 10.), (1, 20.), (1, 30.), (2, 30.), (2, 40.), (2, 50.)]
        rows = [dict(scenario="shared", threads=threads, ops_per_sec=rate)
                for threads, rate in rates]
        result = scaling.summarize(rows, [1, 2], ["shared"], 100)
        self.assertEqual(result[0]["median_ops_per_sec"], 20)
        self.assertEqual(result[0]["mad_ops_per_sec"], 10)
        self.assertEqual(result[1]["median_ops_per_sec"], 40)
        self.assertEqual(result[1]["speedup"], 2)
        self.assertEqual(result[1]["efficiency"], 1)
        self.assertAlmostEqual(result[0]["p90_ops_per_sec"], 28)

    def test_thread_parser(self):
        self.assertEqual(scaling.thread_list("8,2,1,4"), [1, 2, 4, 8])
        for item in ("", "1,1", "2,4", "0,1", "1,17", "abc,1"):
            with self.subTest(item=item):
                with self.assertRaises(Exception):
                    scaling.thread_list(item)

    def test_cli_small_real_benchmark(self):
        binary = ROOT / "build" / "workload_bench"
        if not binary.is_file():
            self.skipTest("make scaling-test builds workload_bench first")
        with tempfile.TemporaryDirectory() as folder:
            raw, summary = Path(folder) / "raw.csv", Path(folder) / "summary.csv"
            capture = io.StringIO()
            with redirect_stdout(capture):
                status = scaling.main([
                    "--benchmark", str(binary), "--threads", "1,2", "--requests", "1000",
                    "--repeats", "2", "--warmups", "0", "--raw-csv", str(raw),
                    "--summary-csv", str(summary)])
            self.assertEqual(status, 0)
            self.assertIn("Collected 8 measured runs", capture.getvalue())
            with raw.open(newline="") as f:
                rows = list(csv.DictReader(f))
            with summary.open(newline="") as f:
                aggregate = list(csv.DictReader(f))
            self.assertEqual(len(rows), 8)
            self.assertEqual(len(aggregate), 4)
            for row in rows:
                self.assertEqual(int(row["allowed"]) + int(row["denied"]), 1000)
                self.assertEqual(int(row["errors"]), 0)
            for row in aggregate:
                self.assertGreater(float(row["median_ops_per_sec"]), 0)
                self.assertEqual(int(row["repeats"]), 2)

    def test_subprocess_failure_is_reported(self):
        with mock.patch.object(scaling.subprocess, "run") as patched:
            patched.return_value.returncode = 1
            patched.return_value.stderr = "fake failure"
            with self.assertRaises(RuntimeError):
                scaling.run_once(Path("/some/binary"), "shared", 1, 100)


if __name__ == "__main__":
    unittest.main(verbosity=2)
