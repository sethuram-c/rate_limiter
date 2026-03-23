#!/usr/bin/env python3
"""Phase 40: fail-closed handoff metadata checks; NOT release certification.

Outputs a concise local result; successful review only establishes internally
consistent evidence references, not correctness or deployed production fitness.
"""
from __future__ import annotations
import argparse
import json
from pathlib import Path
import re
import sys

ROOT = Path(__file__).resolve().parents[1]
REQUIRED = {'README.md', 'docs/final_handoff.md', 'docs/resume_interview_kit.md',
            'docs/production_readiness.md', 'docs/production_risks.json',
            'docs/public_api_reference.md', 'docs/system_architecture.md'}
BLOCKERS = {'R01', 'R02', 'R03', 'R05'}


def is_source_path(root: Path, value: object) -> bool:
    if not isinstance(value, str) or not value:
        return False
    rel = Path(value)
    if rel.is_absolute() or '..' in rel.parts or rel.parts[0] == 'build':
        return False
    path = root / rel
    return path.is_file() and not path.is_symlink()


def check(data: object, root: Path = ROOT) -> list[str]:
    """Return all contract problems; tests inject mutated metadata directly."""
    root = Path(root)
    errors = []
    if not isinstance(data, dict):
        return ['handoff manifest must be an object']
    if type(data.get('schema_version')) is not int or data['schema_version'] != 1:
        errors.append('schema_version must be 1')
    if type(data.get('phase')) is not int or data['phase'] != 40:
        errors.append('phase must be 40')
    if data.get('commit_date') != '2027-01-23':
        errors.append('commit_date must be 2027-01-23')
    try:
        version = (root / 'VERSION').read_text(encoding='utf-8').strip()
        risks = json.loads((root / 'docs/production_risks.json').read_text(encoding='utf-8'))
        make = (root / 'Makefile').read_text(encoding='utf-8')
        readme = (root / 'README.md').read_text(encoding='utf-8')
        handoff = (root / 'docs/final_handoff.md').read_text(encoding='utf-8')
        interview = (root / 'docs/resume_interview_kit.md').read_text(encoding='utf-8')
    except (OSError, UnicodeError, ValueError, json.JSONDecodeError) as exc:
        return errors + [f'required project document missing or invalid: {exc}']
    if data.get('project_version') != version or version != risks.get('project_version'):
        errors.append('project_version must match VERSION and risk register')
    if data.get('release_status') != 'Unreleased' or data.get('release_created') is not False:
        errors.append('release must stay Unreleased; no release may be claimed')
    if (data.get('deployment_verdict') != 'NOT_PRODUCTION_READY'
            or risks.get('deployment_verdict') != 'NOT_PRODUCTION_READY'):
        errors.append('deployment verdict must remain NOT_PRODUCTION_READY')
    if data.get('learning_pdf_in_repository') is not False:
        errors.append('learning PDF must stay outside Git')
    if 'NOT_PRODUCTION_READY' not in handoff or 'unreleased' not in interview.lower():
        errors.append('handoff documents must disclose unreleased/not-ready status')
    if 'final_handoff.md' not in readme or 'resume_interview_kit.md' not in readme:
        errors.append('README must link both final handoff documents')
    records = risks.get('risks', [])
    blockers = {x.get('id') for x in records if isinstance(x, dict) and x.get('status') == 'blocker'}
    if blockers != BLOCKERS:
        errors.append('mandatory risk blockers must remain R01 R02 R03 R05')
    docs = data.get('documents')
    if not isinstance(docs, list) or set(map(str, docs)) != REQUIRED or len(docs) != len(REQUIRED):
        errors.append('documents must contain exactly the required handoff references')
    else:
        for path in docs:
            if not is_source_path(root, path):
                errors.append(f'document path invalid or missing: {path}')
    evidence = data.get('evidence')
    if not isinstance(evidence, list) or len(evidence) < 6:
        errors.append('at least six evidence mappings required')
    else:
        seen = set()
        for idx, item in enumerate(evidence):
            if not isinstance(item, dict):
                errors.append(f'evidence[{idx}] must be an object')
                continue
            claim = item.get('claim')
            if not isinstance(claim, str) or len(claim.strip()) < 12 or claim in seen:
                errors.append(f'evidence[{idx}] claim missing or repeated')
            else:
                seen.add(claim)
            for name in ('source', 'test'):
                if not is_source_path(root, item.get(name)):
                    errors.append(f'evidence[{idx}] missing/unsafe {name}')
            target = item.get('target')
            if not isinstance(target, str) or not re.fullmatch('[a-z][a-z0-9-]*', target):
                errors.append(f'evidence[{idx}] invalid Make target')
            elif not re.search(rf'(?m)^{re.escape(target)}\s*:', make):
                errors.append(f'evidence[{idx}] Make target missing: {target}')
    return errors


def main(argv=None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--root', type=Path, default=ROOT)
    parser.add_argument('--manifest', type=Path, default=None)
    args = parser.parse_args(argv)
    root = args.root.resolve()
    path = args.manifest or root / 'docs/handoff_manifest.json'
    try:
        data = json.loads(path.read_text(encoding='utf-8'))
    except (OSError, UnicodeError, json.JSONDecodeError) as exc:
        print(f'HANDOFF AUDIT FAILED: cannot read manifest: {exc}', file=sys.stderr)
        return 1
    errors = check(data, root)
    if errors:
        for error in errors:
            print(f'HANDOFF AUDIT FAILED: {error}', file=sys.stderr)
        return 1
    print('HANDOFF AUDIT PASSED: phase 40, 7 documents, '
          f'{len(data["evidence"])} mapped claims; Unreleased / NOT_PRODUCTION_READY')
    return 0


if __name__ == '__main__':
    sys.exit(main())
