"""Phase 36 architecture documentation contracts; stdlib-only for CI."""
import re
import shutil
import subprocess
import unittest
import xml.etree.ElementTree as ET
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
DIAGRAMS = ROOT / "docs" / "diagrams"
NAMES = ("components", "request_path", "cas_state", "lifecycle")

class ArchitectureDocumentationTests(unittest.TestCase):
    def test_all_dot_and_svg_files_exist_and_are_parseable(self):
        for stem in NAMES:
            with self.subTest(stem=stem):
                dot = (DIAGRAMS / (stem + ".dot")).read_text(encoding="utf-8")
                self.assertRegex(dot, r"(?m)^digraph\s+" + stem + r"\s*\{")
                svg = ET.parse(DIAGRAMS / (stem + ".svg")).getroot()
                self.assertEqual(svg.tag, "{http://www.w3.org/2000/svg}svg")
                self.assertTrue(any(t.tag.endswith("title") for t in svg.iter()))

    def test_diagrams_cover_critical_modules_and_protocol(self):
        component = (DIAGRAMS / "components.dot").read_text()
        for module in ("request_policy.c", "request_gateway.c", "rate_service.c",
                       "ip_rate_limiter.c", "ip_atomic_table.c", "lockfree_bucket.c",
                       "bucket_state.c", "ipv4_parse_internal.h"):
            with self.subTest(module=module):
                self.assertIn(module, component)
        cas = (DIAGRAMS / "cas_state.dot").read_text()
        for token in ("48-bit", "16-bit", "compare_exchange_weak", "retry"):
            self.assertIn(token, cas)
        life = (DIAGRAMS / "lifecycle.dot").read_text()
        for term in ("start worker", "Join ALL workers", "Destroy service"):
            self.assertIn(term, life)

    def test_public_document_references_all_rendered_diagrams(self):
        doc = (ROOT / "docs/system_architecture.md").read_text()
        for stem in NAMES:
            with self.subTest(stem=stem):
                self.assertIn(f"diagrams/{stem}.svg", doc)
                self.assertIn(f"diagrams/{stem}.dot", doc)
        self.assertIn("architecture_decisions.md", doc)
        self.assertIn("not transactional", doc)
        self.assertIn("not an", doc.lower())

    def test_documented_modules_and_critical_symbols_are_real(self):
        for name in ("request_policy", "request_gateway", "rate_service",
                     "ip_rate_limiter", "ip_atomic_table", "lockfree_bucket",
                     "bucket_state", "monotonic_clock"):
            self.assertTrue((ROOT / "src" / (name + ".c")).is_file(), name)
        source = (ROOT / "src" / "lockfree_bucket.c").read_text()
        self.assertIn("atomic_compare_exchange_weak_explicit", source)
        self.assertIn("bucket_state_plan", source)
        self.assertIn("atomic_is_lock_free", source)
        self.assertIn("rl_ipv4_parse_span", (ROOT / "src/ipv4_parse_internal.h").read_text())

    def test_adrs_discuss_decisions_and_tradeoffs(self):
        doc = (ROOT / "docs/architecture_decisions.md").read_text()
        self.assertEqual(len(re.findall(r"(?m)^## ADR-00[1-6]:", doc)), 6)
        for term in ("reclamation", "monotonic", "memory", "CAS", "immutable"):
            self.assertIn(term, doc.lower() if term != "CAS" else doc)

    @unittest.skipUnless(shutil.which("dot"), "optional Graphviz not installed")
    def test_dot_sources_render_when_graphviz_is_installed(self):
        for stem in NAMES:
            with self.subTest(stem=stem):
                rendered = subprocess.run(["dot", "-Tsvg", str(DIAGRAMS / (stem + ".dot"))],
                                          text=True, capture_output=True, timeout=12)
                self.assertEqual(rendered.returncode, 0, rendered.stderr)
                root = ET.fromstring(rendered.stdout)
                self.assertEqual(root.tag, "{http://www.w3.org/2000/svg}svg")

if __name__ == "__main__":
    unittest.main()
