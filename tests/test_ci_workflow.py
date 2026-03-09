#!/usr/bin/env python3
"""Phase 26: stdlib-only contract tests for the GitHub Actions CI definition.

These guard important invariants but are NOT a replacement for a YAML parser,
actionlint, or an actual GitHub-hosted workflow execution.
"""
import re
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
WORKFLOW_PATH = ROOT / '.github' / 'workflows' / 'ci.yml'


class WorkflowContractTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.text = WORKFLOW_PATH.read_text(encoding='utf-8')
        # Remove comments before testing code properties.
        cls.content = '\n'.join(line for line in cls.text.splitlines()
                                if not line.lstrip().startswith('#'))

    def test_triggers_and_least_privilege(self):
        self.assertRegex(self.content, r'(?m)^on:\s*$')
        for event in ('push', 'pull_request', 'workflow_dispatch'):
            self.assertRegex(self.content, rf'(?m)^  {event}:\s*$')
        self.assertIn('permissions:\n  contents: read', self.content)
        self.assertNotIn('pull_request_target:', self.content)
        self.assertNotRegex(self.content, r'(?m)^\s+contents: write\s*$')

    def test_both_compilers_and_strict_build(self):
        self.assertIn('compiler: [gcc, clang]', self.content)
        self.assertIn('make CC="$CC" clean', self.content)
        self.assertIn('make CC="$CC" -j2 all', self.content)
        self.assertIn('make CC="$CC" test', self.content)
        self.assertIn('fail-fast: false', self.content)
        self.assertNotIn('continue-on-error: true', self.content)

    def test_sanitizers_and_no_unsafe_bypass(self):
        self.assertIn('make CC=gcc audit-asan', self.content)
        self.assertRegex(self.content, r'(?m)^  sanitizers:\s*$')
        self.assertNotRegex(self.content, r'(?m)^\s*if:.*always\(\)')
        self.assertNotIn('|| true', self.content)

    def test_bounded_runners_and_no_inline_secrets(self):
        self.assertEqual(self.content.count('runs-on: ubuntu-24.04'), 2)
        self.assertEqual(self.content.count('timeout-minutes:'), 2)
        self.assertNotIn('secrets.', self.content)
        self.assertRegex(self.content, r'actions/checkout@v[0-9]+')


if __name__ == '__main__':
    unittest.main()
