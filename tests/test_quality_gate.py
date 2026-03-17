"""Unit tests for Phase 34 quality-gate orchestration and fail-closed paths."""
import contextlib
import importlib.util
import io
from pathlib import Path
from types import SimpleNamespace
import tempfile
import subprocess
import unittest

ROOT = Path(__file__).resolve().parent.parent
SPEC = importlib.util.spec_from_file_location('quality_gate', ROOT / 'tools' / 'quality_gate.py')
GATE = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(GATE)


class QualityGateTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.root = Path(self.tmp.name)
        (self.root / 'Makefile').write_text('all:\n\t@true\n', encoding='utf-8')
        self.calls = []

    def runner(self, args, *, cwd, check, timeout):
        self.calls.append((args, cwd, check, timeout))
        return SimpleNamespace(returncode=0)

    @staticmethod
    def available(_command):
        return '/usr/bin/tool'

    def execute(self, profile='quick', compiler='gcc', runner=None, lookup=None, timeout=None):
        with contextlib.redirect_stdout(io.StringIO()), contextlib.redirect_stderr(io.StringIO()):
            return GATE.run_gate(profile, compiler, self.root,
                                 runner=self.runner if runner is None else runner,
                                 find_tool=self.available if lookup is None else lookup,
                                 timeout_seconds=timeout)

    def test_quick_profile_executes_clean_then_build_and_contracts(self):
        self.assertEqual(self.execute(), 0)
        self.assertEqual(self.calls[0][0], ['make', 'CC=gcc', 'clean'])
        self.assertEqual(self.calls[1][0], ['make', 'CC=gcc', '-j2', 'all'])
        self.assertTrue(any('impossible-request-test' in args for args, _, _, _ in self.calls))
        self.assertTrue(any('package-test' in args for args, _, _, _ in self.calls))
        self.assertEqual(self.calls[-1][0], ['make', 'CC=gcc', 'quality-gate-test'])
        self.assertTrue(all(root == self.root.resolve() and check is False
                            for _, root, check, _ in self.calls))

    def test_release_profile_requires_every_important_layer(self):
        self.assertEqual(self.execute('release', 'clang'), 0)
        self.assertEqual([call[0][-1] for call in self.calls],
                         ['clean', 'all', 'test', 'static-check', 'audit-asan'])
        self.assertTrue(all(call[0][1] == 'CC=clang' for call in self.calls))

    def test_first_nonzero_exit_stops_execution(self):
        def fails_on_second(args, *, cwd, check, timeout):
            self.calls.append(args)
            return SimpleNamespace(returncode=9 if len(self.calls) == 2 else 0)
        self.assertEqual(self.execute(runner=fails_on_second), 1)
        self.assertEqual(len(self.calls), 2)

    def test_step_timeout_stops_execution(self):
        def times_out(*_args, **_kwargs):
            raise subprocess.TimeoutExpired(['make'], 3)
        self.assertEqual(self.execute(runner=times_out), 1)

    def test_non_positive_timeout_is_rejected(self):
        self.assertEqual(self.execute(timeout=0), 2)
        self.assertEqual(self.calls, [])

    def test_subprocess_oserror_never_counts_as_pass(self):
        def missing(*_args, **_kwargs):
            raise OSError('unavailable')
        self.assertEqual(self.execute(runner=missing), 2)

    def test_missing_makefile_fails_without_running_any_step(self):
        (self.root / 'Makefile').unlink()
        self.assertEqual(self.execute(), 2)
        self.assertEqual(self.calls, [])

    def test_missing_required_tool_fails_without_running(self):
        self.assertEqual(self.execute(lookup=lambda cmd: None if cmd == 'clang'
                                      else '/usr/bin/tool', compiler='clang'), 2)
        self.assertEqual(self.calls, [])

    def test_invalid_profile_is_rejected(self):
        self.assertEqual(self.execute(profile='ignored'), 2)
        self.assertFalse(self.calls)

    def test_invalid_compiler_is_rejected(self):
        self.assertEqual(self.execute(compiler='gcc;false'), 2)
        self.assertFalse(self.calls)

    def test_every_step_is_argument_vector_not_shell_command(self):
        self.assertEqual(self.execute(), 0)
        for args, _, _, _ in self.calls:
            self.assertIsInstance(args, list)
            self.assertTrue(all(isinstance(part, str) for part in args))
            self.assertNotIn('sh', args[:1])

    def test_release_does_not_depend_on_benchmark_sampling(self):
        for _, targets in GATE.PROFILES['release']:
            self.assertNotIn('scaling-report', targets)
            self.assertNotIn('traffic-profile-report', targets)
            self.assertNotIn('mutex-compare-report', targets)


if __name__ == '__main__':
    unittest.main()
