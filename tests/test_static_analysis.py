#!/usr/bin/env python3
"""Phase 27: static-analysis runner contract and fail-closed tests."""
import importlib.util
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch

ROOT = Path(__file__).resolve().parent.parent
spec = importlib.util.spec_from_file_location('static_analysis',
                                              ROOT / 'tools' / 'static_analysis.py')
sa = importlib.util.module_from_spec(spec)
spec.loader.exec_module(sa)


class StaticAnalysisTests(unittest.TestCase):
    def test_source_discovery_is_stable_and_includes_all_directories(self):
        with tempfile.TemporaryDirectory() as d:
            root = Path(d)
            for name in ('src/z.c', 'src/a.c', 'bench/b.c', 'tests/t.c'):
                path = root / name
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_text('int valid(void) { return 0; }\n')
            found = sa.collect_sources(root, ['src', 'bench', 'tests'])
            self.assertEqual([str(x) for x in found],
                             ['bench/b.c', 'src/a.c', 'src/z.c', 'tests/t.c'])

    def test_compiler_arguments_always_fail_on_diagnostics(self):
        for tool in ('gcc', 'clang'):
            with self.subTest(tool=tool):
                command = sa.command_for(tool, tool, Path('src/x.c'))
                self.assertIn('-Werror', command)
                self.assertIn('-std=c11', command)
                self.assertIn('-pthread', command)
                self.assertIn('-Iinclude', command)
                self.assertIn('src/x.c', command)
                self.assertIn('-fanalyzer' if tool == 'gcc' else '--analyze',
                              command)
                if tool == 'clang':
                    self.assertIn('-analyzer-werror', command)

    def test_missing_compiler_fails_closed(self):
        with tempfile.TemporaryDirectory() as d:
            root = Path(d)
            (root / 'src').mkdir()
            (root / 'src' / 'a.c').write_text('int f(void) {return 0;}\n')
            self.assertEqual(sa.run(root, ('src',), ('gcc',),
                                    {'gcc': '__missing_compiler_phase27__'}), 2)

    def test_missing_sources_fails_closed(self):
        with tempfile.TemporaryDirectory() as d:
            self.assertEqual(sa.run(d, ('src',), ('gcc',), {'gcc': 'gcc'}), 2)

    def test_analyzer_failure_is_not_reported_as_pass(self):
        with tempfile.TemporaryDirectory() as d:
            root = Path(d)
            (root / 'src').mkdir()
            (root / 'src' / 'a.c').write_text('int f(void) {return 0;}\n')
            class Result:
                returncode = 1
                stdout = ''
                stderr = 'warning promoted to error'
            with patch.object(sa.shutil, 'which', return_value='/usr/bin/gcc'), \
                 patch.object(sa.subprocess, 'run', return_value=Result()):
                self.assertEqual(sa.run(root, ('src',), ('gcc',),
                                        {'gcc': 'gcc'}), 1)

    def test_latency_benchmark_kept_in_clang_scope(self):
        self.assertEqual(sa.CLANG_DEFAULT_DIRECTORIES, ('src',))
        self.assertEqual(str(sa.CLANG_BENCHMARK_REGRESSION),
                         'bench/latency_bench.c')

    def test_valid_simple_translation_unit(self):
        with tempfile.TemporaryDirectory() as d:
            root = Path(d)
            (root / 'src').mkdir()
            (root / 'src' / 'ok.c').write_text('int answer(void) {return 42;}\n')
            self.assertEqual(sa.run(root, ('src',), ('gcc',), {'gcc': 'gcc'}), 0)


if __name__ == '__main__':
    unittest.main()
