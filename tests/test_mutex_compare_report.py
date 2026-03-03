import pathlib
import subprocess
import tempfile
import unittest

from sys import path
path.insert(0, str(pathlib.Path(__file__).resolve().parents[1] / "bench"))
from mutex_compare_report import parse_line, compare


class ReporterTests(unittest.TestCase):
    GOOD = ("impl=lockfree scenario=shared threads=2 requests=10 allowed=7 "
            "denied=3 errors=0 elapsed_ns=1000 ops_per_sec=10000000 valid=yes")

    def test_valid(self):
        r = parse_line(self.GOOD, "lockfree", "shared", 2, 10)
        self.assertEqual(r["allowed"], 7)

    def test_bad_accounting(self):
        with self.assertRaises(ValueError):
            parse_line(self.GOOD.replace("denied=3", "denied=4"),
                       "lockfree", "shared", 2, 10)

    def test_wrong_implementation(self):
        with self.assertRaises(ValueError):
            parse_line(self.GOOD, "mutex", "shared", 2, 10)

    def test_negative_count(self):
        with self.assertRaises(ValueError):
            parse_line(self.GOOD.replace("denied=3", "denied=-3"),
                       "lockfree", "shared", 2, 10)

    def test_invalid_flag(self):
        with self.assertRaises(ValueError):
            parse_line(self.GOOD.replace("valid=yes", "valid=no"),
                       "lockfree", "shared", 2, 10)

    def test_pair_ratio(self):
        samples = [({"throughput": 10}, {"throughput": 20}),
                   ({"throughput": 20}, {"throughput": 30}),
                   ({"throughput": 30}, {"throughput": 60})]
        self.assertEqual(compare(samples), (20, 30, 2))

    def test_real_small_report(self):
        benchmark = pathlib.Path("build/mutex_compare").resolve()
        with tempfile.TemporaryDirectory() as folder:
            subprocess.run(["python3", "bench/mutex_compare_report.py",
                            "--benchmark", str(benchmark), "--threads", "1,2",
                            "--requests", "300", "--repeats", "2", "--warmups", "0",
                            "--output-dir", folder], check=True,
                           capture_output=True, text=True, timeout=40)
            summary = (pathlib.Path(folder) / "mutex_comparison_summary.csv").read_text()
            raw = (pathlib.Path(folder) / "mutex_comparison_raw.csv").read_text()
            self.assertEqual(summary.count("\n"), 5)
            self.assertEqual(raw.count("\n"), 17)


if __name__ == "__main__":
    unittest.main()
