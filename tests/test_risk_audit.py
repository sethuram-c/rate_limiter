"""Phase 38: validate risk-review checks and their intentional negative paths."""
import copy
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
import risk_audit  # noqa: E402


class RiskReviewTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.register = json.loads((ROOT / 'docs/production_risks.json').read_text(encoding='utf-8'))

    def clone(self):
        return copy.deepcopy(self.register)

    def test_real_register_matches_code_and_make_targets(self):
        self.assertEqual([], risk_audit.validate_register(self.clone(), ROOT))
        self.assertEqual(12, len(self.register['risks']))

    def test_cannot_claim_production_readiness(self):
        altered = self.clone()
        altered['deployment_verdict'] = 'PRODUCTION_READY'
        self.assertIn('deployment_verdict', ' '.join(risk_audit.validate_register(altered, ROOT)))

    def test_blockers_cannot_be_downgraded(self):
        altered = self.clone()
        altered['risks'][0]['status'] = 'accepted-limitation'
        self.assertIn('mandatory blocker', ' '.join(risk_audit.validate_register(altered, ROOT)))

    def test_missing_risk_fails(self):
        altered = self.clone()
        altered['risks'].pop()
        self.assertIn('missing required risks', ' '.join(risk_audit.validate_register(altered, ROOT)))

    def test_duplicate_id_fails(self):
        altered = self.clone()
        altered['risks'][1]['id'] = altered['risks'][0]['id']
        self.assertIn('duplicate risk', ' '.join(risk_audit.validate_register(altered, ROOT)))

    def test_missing_or_unsafe_file_fails(self):
        for bad in ('../../etc/passwd', '/tmp/file.c', 'build/generated.c', 'src/fake.c'):
            altered = self.clone()
            altered['risks'][0]['source'] = bad
            with self.subTest(bad=bad):
                self.assertIn('invalid/missing source', ' '.join(risk_audit.validate_register(altered, ROOT)))

    def test_narrative_and_mitigation_are_required(self):
        altered = self.clone()
        altered['risks'][0]['mitigation'] = 'none'
        self.assertIn('mitigation must explain', ' '.join(risk_audit.validate_register(altered, ROOT)))

    def test_unknown_make_target_fails(self):
        altered = self.clone()
        altered['risks'][0]['verification_target'] = 'imaginary-risk-check'
        self.assertIn('does not exist', ' '.join(risk_audit.validate_register(altered, ROOT)))

    def test_malformed_register_fails_closed(self):
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / 'broken.json'
            path.write_text('{no', encoding='utf-8')
            result = subprocess.run([sys.executable, str(ROOT / 'tools/risk_audit.py'),
                                     '--register', str(path)], capture_output=True, text=True)
            self.assertEqual(1, result.returncode)
            self.assertIn('RISK AUDIT FAILED', result.stderr)

    def test_cli_review_passes_but_strict_production_refuses(self):
        script = str(ROOT / 'tools/risk_audit.py')
        valid = subprocess.run([sys.executable, script, '--summary'], capture_output=True, text=True)
        self.assertEqual(0, valid.returncode, valid.stderr)
        self.assertIn('NOT_PRODUCTION_READY', valid.stdout)
        self.assertIn('R12', valid.stdout)
        blocked = subprocess.run([sys.executable, script, '--strict-production'],
                                 capture_output=True, text=True)
        self.assertEqual(3, blocked.returncode)
        self.assertIn('PRODUCTION READINESS REFUSED', blocked.stderr)

    def test_invalid_top_level_rejected(self):
        self.assertTrue(risk_audit.validate_register([], ROOT))
        altered = self.clone()
        altered['schema_version'] = '1'
        self.assertIn('schema_version', ' '.join(risk_audit.validate_register(altered, ROOT)))


if __name__ == '__main__':
    unittest.main()
