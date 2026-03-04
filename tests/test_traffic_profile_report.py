import csv
import pathlib
import subprocess
import tempfile
import unittest
import sys

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1] / "bench"))
from traffic_profile_report import parse_output, median_pair


class ProfileReportTests(unittest.TestCase):
    LINE = ("impl=lockfree pattern=skewed costs=mixed threads=2 requests=10 "
            "ips=16 seed=7 allowed=6 denied=4 errors=0 allowed_tokens=10 "
            "elapsed_ns=1000 ops_per_sec=10000000 valid=yes")

    def parse(self, text=LINE):
        return parse_output(text, "lockfree", "skewed", "mixed", 2, 10, 16, 7)

    def test_good(self):
        self.assertEqual(self.parse()["allowed_tokens"], 10)

    def test_bad_fields(self):
        for line in (self.LINE + " junk=1", self.LINE.replace("valid=yes", "valid=no"),
                     self.LINE.replace("allowed=6", "allowed=7"),
                     self.LINE.replace("allowed_tokens=10", "allowed_tokens=19"),
                     self.LINE.replace("elapsed_ns=1000", "elapsed_ns=0"),
                     self.LINE.replace("ops_per_sec=10000000", "ops_per_sec=nan"),
                     self.LINE.replace("pattern=skewed", "pattern=hot")):
            with self.subTest(line=line), self.assertRaises(ValueError):
                self.parse(line)

    def test_pair(self):
        m, lf, ratio = median_pair([
            ({"throughput": 10}, {"throughput": 20}),
            ({"throughput": 20}, {"throughput": 30}),
            ({"throughput": 30}, {"throughput": 60})])
        self.assertEqual((m, lf, ratio), (20, 30, 2))

    def test_real_small_matrix(self):
        binary = pathlib.Path("build/realistic_traffic_bench").resolve()
        with tempfile.TemporaryDirectory() as tmp:
            subprocess.run(["python3", "bench/traffic_profile_report.py",
                            "--benchmark", str(binary), "--threads", "1,2",
                            "--requests", "600", "--ips", "16",
                            "--repeats", "2", "--warmups", "0",
                            "--output-dir", tmp], check=True,
                           capture_output=True, text=True, timeout=90)
            with (pathlib.Path(tmp) / "traffic_profile_raw.csv").open() as f:
                rows = list(csv.DictReader(f))
                self.assertEqual(len(rows), 3 * 2 * 2 * 2 * 2)
                self.assertTrue(all(float(r["throughput"]) > 0 for r in rows))
            with (pathlib.Path(tmp) / "traffic_profile_summary.csv").open() as f:
                rows = list(csv.DictReader(f))
                self.assertEqual(len(rows), 3 * 2 * 2)


if __name__ == "__main__":
    unittest.main()
