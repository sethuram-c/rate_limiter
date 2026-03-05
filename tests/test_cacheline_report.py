"""Phase 22: reject malformed benchmark samples and verify paired statistics."""
import pathlib
import sys
import tempfile
import unittest

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1] / "bench"))
import cacheline_report as cr


class CachelineReportTests(unittest.TestCase):
    def test_paired_summary(self):
        pairs = [({"throughput": 100.0}, {"throughput": 110.0}),
                 ({"throughput": 200.0}, {"throughput": 180.0}),
                 ({"throughput": 300.0}, {"throughput": 360.0})]
        result = cr.paired_summary(pairs)
        self.assertEqual(result["compact_median_ops"], 200)
        self.assertEqual(result["padded_median_ops"], 180)
        self.assertEqual(result["median_paired_padded_over_compact"], 1.1)
        self.assertEqual(result["median_paired_percent_change"], 10.0)

    def test_reject_empty_pairs(self):
        with self.assertRaises(ValueError):
            cr.paired_summary([])

    def test_one_real_matched_pair_with_csv(self):
        root = pathlib.Path(__file__).resolve().parents[1]
        binaries = {"compact": root / "build/realistic_traffic_bench",
                    "padded": root / "build/realistic_traffic_bench_padded"}
        with tempfile.TemporaryDirectory() as temp:
            raw, summary = cr.run_matrix(binaries, ["uniform"], [2],
                                         2000, 8, "mixed", 2, 0,
                                         pathlib.Path(temp))
            self.assertEqual(len(raw), 4)
            self.assertEqual(len(summary), 1)
            self.assertTrue((pathlib.Path(temp) / "cacheline_raw.csv").is_file())
            self.assertTrue((pathlib.Path(temp) / "cacheline_summary.csv").is_file())
            self.assertTrue(all(r["allowed"] + r["denied"] == 2000
                                for r in raw))
            self.assertEqual({r["layout"] for r in raw}, {"compact", "padded"})


if __name__ == "__main__":
    unittest.main()
