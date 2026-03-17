#!/usr/bin/env python3
"""Phase 34: fail-closed, repeatable release-quality checks.

Each step uses an argument-vector subprocess (not a shell), with a clean
build to prevent reusing GCC objects when testing Clang or vice versa.
The release profile is deliberately more expensive than the quick profile.
"""
import argparse
from pathlib import Path
import shutil
import subprocess
import sys

ROOT = Path(__file__).resolve().parent.parent

# A tuple of (description, make arguments). Order is a contract.
PROFILES = {
    'quick': (
        ('Clean previous binaries', ('clean',)),
        ('Compile strict C11 demo', ('-j2', 'all')),
        ('Exercise admission and failure contracts',
         ('impossible-request-test', 'api-contract-test', 'request-gateway-test',
          'request-policy-test', 'ip-rate-test')),
        ('Verify staged library, public docs and CI contract',
         ('package-test', 'docs-test', 'version-test', 'ci-config-test')),
        ('Verify the quality-gate runner itself', ('quality-gate-test',)),
    ),
    'release': (
        ('Clean previous binaries', ('clean',)),
        ('Compile strict C11 demo', ('-j2', 'all')),
        ('Run complete regression suite', ('test',)),
        ('Run GCC and Clang static analyzers', ('static-check',)),
        ('Run ASan and UBSan concurrent audit', ('audit-asan',)),
    ),
}


def run_gate(profile, compiler, root=ROOT, runner=None, find_tool=None, timeout_seconds=None):
    """Return 0 only if every requested command completed successfully.

    ``runner`` and ``find_tool`` are injectable to test failure propagation
    without compiling the whole repository inside unit tests.
    """
    root = Path(root).resolve()
    runner = subprocess.run if runner is None else runner
    find_tool = shutil.which if find_tool is None else find_tool
    if profile not in PROFILES or compiler not in ('gcc', 'clang') or (
            timeout_seconds is not None and timeout_seconds <= 0):
        print('QUALITY GATE FAILED: invalid profile or compiler', file=sys.stderr)
        return 2
    if not (root / 'Makefile').is_file():
        print(f'QUALITY GATE FAILED: Makefile absent in {root}', file=sys.stderr)
        return 2
    for executable in ('make', compiler):
        if find_tool(executable) is None:
            print(f'QUALITY GATE FAILED: required executable absent: {executable}',
                  file=sys.stderr)
            return 2

    steps = PROFILES[profile]
    timeout_seconds = (180 if profile == 'quick' else 900) if timeout_seconds is None else timeout_seconds
    for index, (description, make_args) in enumerate(steps, 1):
        command = ['make', f'CC={compiler}', *make_args]
        print(f'[{index}/{len(steps)}] {description}: {" ".join(command)}', flush=True)
        try:
            completed = runner(command, cwd=root, check=False, timeout=timeout_seconds)
        except subprocess.TimeoutExpired:
            print(f'QUALITY GATE FAILED: {description} timed out after '
                  f'{timeout_seconds}s', file=sys.stderr)
            return 1
        except OSError as error:
            print(f'QUALITY GATE FAILED: {description}: {error}', file=sys.stderr)
            return 2
        if completed.returncode != 0:
            print(f'QUALITY GATE FAILED: {description} (exit {completed.returncode})',
                  file=sys.stderr)
            return 1
    print(f'QUALITY GATE PASSED: {profile} with {compiler}')
    return 0


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--profile', choices=tuple(PROFILES), default='quick')
    parser.add_argument('--compiler', choices=('gcc', 'clang'), default='gcc')
    parser.add_argument('--root', type=Path, default=ROOT)
    parser.add_argument('--timeout-seconds', type=int, default=None,
                        help='time limit for each required make step; must be > 0')
    options = parser.parse_args(argv)
    return run_gate(options.profile, options.compiler, options.root,
                    timeout_seconds=options.timeout_seconds)


if __name__ == '__main__':
    sys.exit(main())
