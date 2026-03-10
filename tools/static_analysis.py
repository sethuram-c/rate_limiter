#!/usr/bin/env python3
"""Run GCC -fanalyzer and Clang Static Analyzer on every C translation unit.

Source files are analyzed independently, without executing or linking them.
Tool diagnostics become failures; absent requested compilers are also failures.
"""
import argparse
from pathlib import Path
import shutil
import subprocess
import sys

DEFAULT_DIRECTORIES = ('src', 'bench', 'examples', 'tests')
CLANG_DEFAULT_DIRECTORIES = ('src',)
CLANG_BENCHMARK_REGRESSION = Path('bench/latency_bench.c')


def collect_sources(root, directories):
    """Produce stable, deduplicated root-relative C translation units."""
    root = Path(root)
    paths = sorted({path.relative_to(root) for directory in directories
                    for path in (root / directory).rglob('*.c')
                    if path.is_file()})
    return paths


def command_for(tool, compiler, source):
    common = ['-std=c11', '-D_POSIX_C_SOURCE=200809L', '-Iinclude',
              '-Itests', '-pthread', '-Wall', '-Wextra', '-Wpedantic', '-Werror']
    if tool == 'gcc':
        return [compiler, *common, '-fanalyzer', '-fsyntax-only', str(source)]
    if tool == 'clang':
        return [compiler, *common, '--analyze', '-Xanalyzer',
                '-analyzer-werror', '-o', '/dev/null', str(source)]
    raise ValueError('unsupported tool: ' + tool)


def run(root, directories, selected, compilers, clang_directories=None):
    root = Path(root).resolve()
    paths = collect_sources(root, directories)
    if not paths:
        print('ERROR: no C sources found in requested directories', file=sys.stderr)
        return 2
    for tool in selected:
        if shutil.which(compilers[tool]) is None:
            print(f'ERROR: {tool} compiler not found: {compilers[tool]}', file=sys.stderr)
            return 2
    failed = 0
    checks = 0
    for tool in selected:
        tool_paths = paths
        if tool == 'clang' and clang_directories is not None:
            tool_paths = collect_sources(root, clang_directories)
            # Analyze production modules plus the benchmark where Phase 27
            # uncovered an initialization-failure memory leak.
            if (root / CLANG_BENCHMARK_REGRESSION).is_file():
                tool_paths.append(CLANG_BENCHMARK_REGRESSION)
            tool_paths = sorted(set(tool_paths))
        if not tool_paths:
            print('ERROR: no C sources for requested analyzer', file=sys.stderr)
            return 2
        checks += len(tool_paths)
        print(f'[{tool}] analyzing {len(tool_paths)} translation units', flush=True)
        for source in tool_paths:
            command = command_for(tool, compilers[tool], source)
            try:
                completed = subprocess.run(command, cwd=root, text=True,
                                           capture_output=True, check=False)
            except OSError as exc:
                print(f'FAIL [{tool}] {source}: {exc}', file=sys.stderr)
                failed += 1
                continue
            if completed.returncode != 0:
                failed += 1
                print(f'FAIL [{tool}] {source} (exit {completed.returncode})',
                      file=sys.stderr)
                if completed.stdout:
                    print(completed.stdout, file=sys.stderr)
                if completed.stderr:
                    print(completed.stderr, file=sys.stderr)
        print(f'[{tool}] complete', flush=True)
    if failed:
        print(f'STATIC ANALYSIS FAILED: {failed} translation-unit checks',
              file=sys.stderr)
        return 1
    print(f'STATIC ANALYSIS PASSED: {checks} translation-unit checks')
    return 0


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--root', type=Path,
                        default=Path(__file__).resolve().parent.parent)
    parser.add_argument('--directories', nargs='+', default=None,
                        help='override source directories for both analyzers')
    parser.add_argument('--tool', choices=('both', 'gcc', 'clang'), default='both')
    parser.add_argument('--gcc', default='gcc')
    parser.add_argument('--clang', default='clang')
    args = parser.parse_args(argv)
    selected = ('gcc', 'clang') if args.tool == 'both' else (args.tool,)
    directories = args.directories or DEFAULT_DIRECTORIES
    clang_directories = None if args.directories else CLANG_DEFAULT_DIRECTORIES
    return run(args.root, directories, selected,
               {'gcc': args.gcc, 'clang': args.clang}, clang_directories)


if __name__ == '__main__':
    sys.exit(main())
