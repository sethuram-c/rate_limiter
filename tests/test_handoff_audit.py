"""Phase 40: positive, mutation, and CLI tests for final handoff claims."""
import copy
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
import handoff_audit  # noqa: E402


class HandoffAuditTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.manifest = json.loads((ROOT / 'docs/handoff_manifest.json').read_text(encoding='utf-8'))

    def clone(self):
        return copy.deepcopy(self.manifest)

    def errors(self, obj):
        return ' / '.join(handoff_audit.check(obj, ROOT))

    def test_real_handoff_contract(self):
        self.assertEqual([], handoff_audit.check(self.clone(), ROOT))

    def test_version_mismatch_rejected(self):
        m = self.clone()
        m['project_version'] = '99.0.0'
        self.assertIn('project_version', self.errors(m))

    def test_fake_release_or_production_claim_rejected(self):
        for key, value in (('release_status', 'Published'),
                           ('release_created', True),
                           ('deployment_verdict', 'PRODUCTION_READY')):
            m = self.clone()
            m[key] = value
            with self.subTest(key=key):
                self.assertTrue(self.errors(m))

    def test_phase_and_date_rejected(self):
        m = self.clone()
        m['phase'] = 39
        m['commit_date'] = '2027-01-22'
        self.assertIn('phase must be 40', self.errors(m))
        self.assertIn('commit_date', self.errors(m))

    def test_learning_pdf_not_in_repo(self):
        m = self.clone()
        m['learning_pdf_in_repository'] = True
        self.assertIn('outside Git', self.errors(m))

    def test_missing_or_unsafe_document(self):
        m = self.clone()
        m['documents'][1] = '../../tmp/fake.md'
        self.assertIn('required handoff references', self.errors(m))

    def test_missing_source_and_test_evidence(self):
        m = self.clone()
        m['evidence'][0]['source'] = '../../etc/passwd'
        m['evidence'][1]['test'] = 'tests/nonexistent.c'
        self.assertIn('missing/unsafe source', self.errors(m))
        self.assertIn('missing/unsafe test', self.errors(m))

    def test_missing_make_target(self):
        m = self.clone()
        m['evidence'][0]['target'] = 'nonexistent-target'
        self.assertIn('Make target missing', self.errors(m))

    def test_duplicate_claim(self):
        m = self.clone()
        m['evidence'][1]['claim'] = m['evidence'][0]['claim']
        self.assertIn('repeated', self.errors(m))

    def test_malformed_manifest_fails_closed(self):
        with tempfile.TemporaryDirectory() as tmp:
            p = Path(tmp) / 'broken.json'
            p.write_text('{missing', encoding='utf-8')
            result = subprocess.run([sys.executable, str(ROOT / 'tools/handoff_audit.py'),
                                     '--manifest', str(p)], capture_output=True, text=True)
            self.assertEqual(result.returncode, 1)
            self.assertIn('HANDOFF AUDIT FAILED', result.stderr)

    def test_cli_success(self):
        result = subprocess.run([sys.executable, str(ROOT / 'tools/handoff_audit.py')],
                                capture_output=True, text=True)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn('NOT_PRODUCTION_READY', result.stdout)

    def test_non_object_rejected(self):
        self.assertIn('object', self.errors([]))


if __name__ == '__main__':
    unittest.main()
