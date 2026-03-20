"""Phase 37: enforce the portfolio README and presentation contracts without third-party dependencies."""
from pathlib import Path
from urllib.parse import unquote
import re
import unittest

ROOT = Path(__file__).resolve().parents[1]
README = ROOT / "README.md"
PORTFOLIO = ROOT / "docs/portfolio_overview.md"
DEMO = ROOT / "docs/project_demo.md"


def markdown_local_targets(text):
    """Simple inline Markdown links/images; ignore external and fragment-only URLs."""
    for raw in re.findall(r"!?\[[^]]*\]\(([^)]+)\)", text):
        url = unquote(raw.split("#", 1)[0].strip())
        if not url or re.match(r"[a-z][a-z0-9+.-]*://", url, re.I):
            continue
        yield url


class PortfolioDocumentationTests(unittest.TestCase):
    def test_curated_readme_and_version_metadata(self):
        text = README.read_text(encoding="utf-8")
        version = (ROOT / "VERSION").read_text(encoding="utf-8").strip()
        self.assertIn(version, text)
        self.assertIn("Unreleased", text)
        self.assertLessEqual(len(text.splitlines()), 180,
                             "README should stay visitor-first; archive details in docs/")
        for heading in ("## Try it on Fedora", "## How it works", "## Core engineering features",
                        "## Link it into an application", "## Correctness and performance evidence",
                        "## Important limitations", "## Project resources"):
            with self.subTest(heading=heading):
                self.assertIn(heading, text)

    def test_all_local_links_and_images_resolve(self):
        for path in (README, PORTFOLIO, DEMO):
            text = path.read_text(encoding="utf-8")
            targets = list(markdown_local_targets(text))
            self.assertGreaterEqual(len(targets), 3, f"No useful links in {path}")
            for link in targets:
                with self.subTest(file=path.name, link=link):
                    self.assertTrue((path.parent / link).exists(), f"Broken link: {path}: {link}")

    def test_readme_commands_and_documented_sources_exist(self):
        text = README.read_text(encoding="utf-8")
        makefile = (ROOT / "Makefile").read_text(encoding="utf-8")
        targets = ("library", "package-test", "docs-test", "test", "quality-quick",
                   "static-check", "audit-asan", "ci-config-test", "portfolio-docs-test",
                   "request-gateway-demo", "request-policy-demo", "contention-test",
                   "impossible-request-test", "evidence-test", "evidence-smoke")
        for target in targets:
            with self.subTest(target=target):
                self.assertIn("make " + target, text)
                self.assertRegex(makefile, rf"(?m)^{re.escape(target)}:")
        self.assertTrue((ROOT / "examples/public_api_walkthrough.c").exists())
        self.assertTrue((ROOT / "include/ratelimiter.h").exists())
        self.assertTrue((ROOT / "docs/diagrams/components.svg").exists())
        self.assertTrue((ROOT / "docs/diagrams/components.dot").exists())

    def test_scope_claims_and_benchmark_honesty(self):
        docs = [p.read_text(encoding="utf-8").lower() for p in (README, PORTFOLIO, DEMO)]
        for i, body in enumerate(docs):
            with self.subTest(document=i):
                for term in ("not", "mutex", "cas", "http", "unreleased"):
                    self.assertIn(term, body)
        self.assertIn("not a mathematical proof", docs[0])
        self.assertIn("no universal speedup", docs[1])
        self.assertIn("not", docs[2])
        self.assertIn("stable binary abi", docs[0])

    def test_presentation_ties_to_actual_source(self):
        p = PORTFOLIO.read_text(encoding="utf-8")
        demo = DEMO.read_text(encoding="utf-8")
        for fn in ("lockfree_bucket.c", "bucket_state.c"):
            with self.subTest(source=fn):
                self.assertTrue((ROOT / "src" / fn).exists())
                self.assertIn(fn, p)
                self.assertIn(fn, demo)
        for file in ("evidence_report.py", "mutex_compare.c"):
            self.assertTrue((ROOT / "bench" / file).exists())

    def test_no_learning_pdf_in_repository(self):
        self.assertFalse(list(ROOT.rglob("Phase_*Learning_Guide*.pdf")))
        self.assertFalse(list(ROOT.rglob("phase-*-learning*.pdf")))


if __name__ == "__main__":
    unittest.main()
