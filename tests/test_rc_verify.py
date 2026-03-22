#!/usr/bin/env python3
"""Fail-closed, mutation, scope and manifest tests for Phase 39."""
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
from rc_verify import HASHED_SOURCE_FILES, STEPS, source_hashes, verify  # noqa: E402


class RCVerifierTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.root = Path(self.tmp.name)
        for relative in HASHED_SOURCE_FILES:
            destination = self.root / relative
            destination.parent.mkdir(parents=True, exist_ok=True)
            destination.write_bytes((ROOT / relative).read_bytes())
        self.output = self.root / 'build' / 'rc_verification.json'

    @staticmethod
    def found(_executable):
        return '/usr/bin/fake-tool'

    @staticmethod
    def ok(_command, **_kwargs):
        return subprocess.CompletedProcess([], 0)

    def invoke(self, **kwargs):
        arguments = dict(root=self.root, output=self.output,
                         find_tool=self.found, runner=self.ok)
        arguments.update(kwargs)
        return verify(**arguments)

    def test_manifest_pass_and_status_not_production(self):
        self.assertEqual(self.invoke(), 0)
        data = json.loads(self.output.read_text())
        self.assertEqual(data['status'], 'passed')
        self.assertFalse(data['production_ready'])
        self.assertFalse(data['release_tag_created'])
        self.assertFalse(data['full_regression_suite_executed'])
        self.assertEqual(len(data['checks']), len(STEPS['check']))
        self.assertEqual(data['sources_sha256'], source_hashes(self.root))

    def test_demo_steps_do_not_claim_release(self):
        self.assertEqual(self.invoke(profile='demo'), 0)
        data = json.loads(self.output.read_text())
        self.assertEqual(data['profile'], 'demo')
        self.assertEqual(len(data['checks']), len(STEPS['demo']))
        self.assertEqual(data['deployment_verdict'], 'NOT_PRODUCTION_READY')

    def test_nonzero_command_fails_and_stops(self):
        calls = []
        def failing(command, **kwargs):
            calls.append(command)
            return subprocess.CompletedProcess(command, 7)
        self.assertEqual(self.invoke(runner=failing), 1)
        self.assertEqual(len(calls), 1)
        data = json.loads(self.output.read_text())
        self.assertEqual(data['status'], 'failed')
        self.assertEqual(data['checks'][0]['exit_code'], 7)

    def test_timeout_fails_without_false_pass(self):
        def timeout(command, **kwargs):
            raise subprocess.TimeoutExpired(command, 1)
        self.assertEqual(self.invoke(runner=timeout), 1)
        self.assertEqual(json.loads(self.output.read_text())['checks'][0]['status'], 'timeout')

    def test_unavailable_compiler_fails_without_manifest(self):
        self.assertEqual(self.invoke(find_tool=lambda _: None), 2)
        self.assertFalse(self.output.exists())

    def test_missing_version_header_fails(self):
        (self.root / 'include/ratelimiter.h').unlink()
        self.assertEqual(self.invoke(), 2)
        self.assertFalse(self.output.exists())

    def test_risk_verdict_cannot_be_overridden(self):
        path = self.root / 'docs/production_risks.json'
        data = json.loads(path.read_text())
        data['deployment_verdict'] = 'PRODUCTION_READY'
        path.write_text(json.dumps(data))
        self.assertEqual(self.invoke(), 2)

    def test_invalid_timeout_or_profile_fails(self):
        self.assertEqual(self.invoke(timeout=0), 2)
        self.assertEqual(self.invoke(profile='not-a-profile'), 2)

    def test_previous_pass_removed_before_failure(self):
        self.assertEqual(self.invoke(), 0)
        def fail(command, **kwargs):
            return subprocess.CompletedProcess(command, 3)
        self.assertEqual(self.invoke(runner=fail), 1)
        self.assertEqual(json.loads(self.output.read_text())['status'], 'failed')

    def test_no_report_committed_in_source_tree(self):
        wrong_output = self.root / 'docs' / 'result.json'
        self.assertEqual(self.invoke(output=wrong_output), 2)
        self.assertFalse(wrong_output.exists())

    def test_invalid_metadata_removes_stale_pass(self):
        self.assertEqual(self.invoke(), 0)
        (self.root / 'VERSION').unlink()
        self.assertEqual(self.invoke(), 2)
        self.assertFalse(self.output.exists())

    def test_hashes_update_when_source_changes(self):
        original = source_hashes(self.root)['Makefile']
        with (self.root / 'Makefile').open('a') as stream:
            stream.write('\n# source mutation\n')
        self.assertNotEqual(source_hashes(self.root)['Makefile'], original)


if __name__ == '__main__':
    unittest.main()
