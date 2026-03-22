#!/usr/bin/env python3
"""Phase 39: bounded, fail-closed verification of an unreleased library snapshot.

This tool checks integration/release *evidence*. It does not certify a production
service, replace the full regression suite, create a Git tag, or ship a release.
"""
from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import sys

ROOT = Path(__file__).resolve().parent.parent
HASHED_SOURCE_FILES = (
    'VERSION', 'CHANGELOG.md', 'Makefile', 'include/ratelimiter.h',
    'src/lockfree_bucket.c', 'src/ip_rate_limiter.c',
    'docs/production_risks.json', '.github/workflows/ci.yml',
)
STEPS = {
    'check': (
        ('version contract', 'release-contract-test'),
        ('installed static-library consumer', 'package-test'),
        ('public API walkthrough', 'docs-test'),
        ('impossible-request capacity guard', 'impossible-request-test'),
        ('CAS contention and bounded-history tests', 'contention-test'),
        ('risk register consistency', 'risk-audit'),
        ('CI configuration', 'ci-config-test'),
    ),
    'demo': (
        ('external consumer demonstration', 'docs-test'),
        ('gateway request outcomes', 'request-gateway-demo'),
        ('CIDR policy decisions', 'request-policy-demo'),
        ('concurrent admission behavior', 'impossible-request-test'),
        ('risk and limitations reminder', 'risk-audit'),
    ),
}


def source_hashes(root):
    """Hash meaningful source artifacts, not volatile compiled outputs."""
    result = {}
    for relative in HASHED_SOURCE_FILES:
        path = root / relative
        if not path.is_file() or path.is_symlink():
            raise ValueError(f'required source file missing or symlink: {relative}')
        result[relative] = hashlib.sha256(path.read_bytes()).hexdigest()
    return result


def verify(root=ROOT, profile='check', compiler='gcc', timeout=120, output=None,
           runner=None, find_tool=None):
    """Write a JSON record and return 0 only after all steps exit successfully.

    Injection hooks support tests of the failure path without executing Make.
    """
    root = Path(root).resolve()
    runner = subprocess.run if runner is None else runner
    find_tool = shutil.which if find_tool is None else find_tool
    output = root / 'build' / 'rc_verification.json' if output is None else Path(output)
    if not output.is_absolute():
        output = root / output
    if profile not in STEPS or compiler not in ('cc', 'gcc', 'clang') or timeout <= 0:
        print('RC VERIFY ERROR: invalid profile, compiler or timeout', file=sys.stderr)
        return 2
    if output.resolve().is_relative_to(root) and output.resolve().relative_to(root).parts[0] != 'build':
        print('RC VERIFY ERROR: reports inside the repository must be in build/', file=sys.stderr)
        return 2
    # Discard stale verification evidence BEFORE tool or metadata validation.
    # A failed rerun must never leave yesterday's PASS behind.
    try:
        output.unlink(missing_ok=True)
    except OSError as error:
        print(f'RC VERIFY ERROR: cannot clear prior report: {error}', file=sys.stderr)
        return 2
    if not (root / 'Makefile').is_file():
        print('RC VERIFY ERROR: repository Makefile missing', file=sys.stderr)
        return 2
    for tool in ('make', compiler):
        if find_tool(tool) is None:
            print(f'RC VERIFY ERROR: required executable missing: {tool}', file=sys.stderr)
            return 2
    try:
        hashes = source_hashes(root)
        version = (root / 'VERSION').read_text(encoding='utf-8').strip()
        risk_data = json.loads((root / 'docs/production_risks.json').read_text(encoding='utf-8'))
        if risk_data.get('deployment_verdict') != 'NOT_PRODUCTION_READY':
            raise ValueError('risk register no longer records NOT_PRODUCTION_READY')
        if risk_data.get('project_version') != version:
            raise ValueError('risk register and VERSION disagree')
    except (OSError, ValueError, json.JSONDecodeError) as error:
        print(f'RC VERIFY ERROR: invalid source manifest: {error}', file=sys.stderr)
        return 2

    results = []
    status = 'passed'
    for label, target in STEPS[profile]:
        command = ['make', f'CC={compiler}', target]
        print(f'RC [{profile}] {label}: {" ".join(command)}', flush=True)
        result = {'name': label, 'make_target': target, 'command': command}
        try:
            completed = runner(command, cwd=root, check=False, timeout=timeout)
            result['exit_code'] = completed.returncode
            if completed.returncode != 0:
                status = 'failed'
                result['status'] = 'failed'
            else:
                result['status'] = 'passed'
        except subprocess.TimeoutExpired:
            result['status'] = 'timeout'
            status = 'failed'
        except OSError as error:
            result['status'] = 'error'
            result['error'] = str(error)
            status = 'failed'
        results.append(result)
        if status != 'passed':
            break
    manifest = {
        'schema_version': 1,
        'phase': 39,
        'version': version,
        'profile': profile,
        'compiler': compiler,
        'status': status,
        'production_ready': False,
        'deployment_verdict': 'NOT_PRODUCTION_READY',
        'full_regression_suite_executed': False,
        'release_tag_created': False,
        'sources_sha256': hashes,
        'checks': results,
    }
    try:
        output.parent.mkdir(parents=True, exist_ok=True)
        output.write_text(json.dumps(manifest, indent=2, sort_keys=True) + '\n', encoding='utf-8')
    except OSError as error:
        print(f'RC VERIFY ERROR: cannot save report: {error}', file=sys.stderr)
        return 2
    if status != 'passed':
        print(f'RC VERIFY FAILED: {results[-1]["name"]}; report: {output}', file=sys.stderr)
        return 1
    print(f'RC VERIFY PASSED: {len(results)} checks, {version}, {profile}; report: {output}')
    print('NOT PRODUCTION READY; no tag or release created.')
    return 0


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--profile', choices=tuple(STEPS), default='check')
    parser.add_argument('--compiler', choices=('cc', 'gcc', 'clang'), default='cc')
    parser.add_argument('--timeout', type=int, default=120,
                        help='per-step limit in seconds')
    parser.add_argument('--root', type=Path, default=ROOT)
    parser.add_argument('--output', type=Path, default=None)
    options = parser.parse_args(argv)
    return verify(root=options.root, profile=options.profile,
                  compiler=options.compiler, timeout=options.timeout,
                  output=options.output)


if __name__ == '__main__':
    sys.exit(main())
