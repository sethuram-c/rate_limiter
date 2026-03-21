#!/usr/bin/env python3
"""Validate the Phase 38 production-risk register; never certify readiness.

Exit codes: 0 = complete/consistent risk documentation; 1 = invalid review;
3 = review is valid but strict production-readiness is blocked.
No third-party dependencies and no runtime code paths are affected.
"""
import argparse
import json
from pathlib import Path
import re
import sys

ROOT = Path(__file__).resolve().parent.parent
RISK_IDS = {f'R{index:02d}' for index in range(1, 13)}
BLOCKERS = {'R01', 'R02', 'R03', 'R05'}
REQUIRED_RECORD_FIELDS = (
    'id', 'title', 'category', 'status', 'severity', 'likelihood',
    'scenario', 'mitigation', 'source', 'test', 'verification_target',
)
STATUSES = {'blocker', 'accepted-limitation'}
SEVERITIES = {'low', 'medium', 'high', 'critical'}
LIKELIHOODS = {'low', 'medium', 'high'}


def valid_repo_path(path, root):
    """Prevent absolute, traversal, generated, and missing evidence paths."""
    if not isinstance(path, str) or not path:
        return False
    rel = Path(path)
    if rel.is_absolute() or '..' in rel.parts or rel.parts[0] == 'build':
        return False
    if rel.suffix not in {'.h', '.c', '.md', '.py'}:
        return False
    return (root / rel).is_file()


def validate_register(data, root=ROOT):
    """Return a list of precise review errors, empty on valid documentation."""
    root = Path(root)
    errors = []
    if not isinstance(data, dict):
        return ['Register must be a JSON object']
    if type(data.get('schema_version')) is not int or data['schema_version'] != 1:
        errors.append('schema_version must be integer 1')
    version_file = root / 'VERSION'
    version = version_file.read_text(encoding='utf-8').strip() if version_file.is_file() else None
    if data.get('project_version') != version or not version:
        errors.append('project_version must match VERSION')
    if data.get('deployment_verdict') != 'NOT_PRODUCTION_READY':
        errors.append('deployment_verdict must remain NOT_PRODUCTION_READY')
    if not isinstance(data.get('review_scope'), str) or not data['review_scope'].strip():
        errors.append('review_scope must be nonempty')
    if not re.fullmatch(r'\d{4}-\d{2}-\d{2}', str(data.get('review_date', ''))):
        errors.append('review_date must be YYYY-MM-DD')
    risks = data.get('risks')
    if not isinstance(risks, list):
        return errors + ['risks must be a list']
    makefile = root / 'Makefile'
    text = makefile.read_text(encoding='utf-8') if makefile.is_file() else ''
    if not text:
        errors.append('Makefile not found or empty')
    seen = set()
    blocked = set()
    for idx, record in enumerate(risks):
        label = f'risks[{idx}]'
        if not isinstance(record, dict):
            errors.append(f'{label}: record must be an object')
            continue
        for field in REQUIRED_RECORD_FIELDS:
            value = record.get(field)
            if not isinstance(value, str) or not value.strip():
                errors.append(f'{label}: missing/empty {field}')
        ident = record.get('id')
        if not isinstance(ident, str) or ident not in RISK_IDS:
            errors.append(f'{label}: unknown risk ID {ident!r}')
        elif ident in seen:
            errors.append(f'{label}: duplicate risk ID {ident}')
        else:
            seen.add(ident)
        status = record.get('status')
        if status not in STATUSES:
            errors.append(f'{label}: unsupported status {status!r}')
        if ident in BLOCKERS and status != 'blocker':
            errors.append(f'{label}: mandatory blocker {ident} cannot be downgraded')
        if status == 'blocker' and isinstance(ident, str):
            blocked.add(ident)
        if record.get('severity') not in SEVERITIES:
            errors.append(f'{label}: unsupported severity')
        if record.get('likelihood') not in LIKELIHOODS:
            errors.append(f'{label}: unsupported likelihood')
        for field in ('scenario', 'mitigation'):
            if not isinstance(record.get(field), str) or len(record[field].strip()) < 45:
                errors.append(f'{label}: {field} must explain the failure/response')
        for field in ('source', 'test'):
            if not valid_repo_path(record.get(field), root):
                errors.append(f'{label}: invalid/missing {field} evidence path')
        target = record.get('verification_target')
        if not isinstance(target, str) or not re.fullmatch(r'[a-z][a-z0-9-]*', target):
            errors.append(f'{label}: invalid verification_target')
        elif not re.search(rf'(?m)^{re.escape(target)}\s*:', text):
            errors.append(f'{label}: Makefile target {target!r} does not exist')
    missing = sorted(RISK_IDS - seen)
    if missing:
        errors.append('missing required risks: ' + ', '.join(missing))
    if blocked != BLOCKERS:
        errors.append('production blockers must be exactly ' + ', '.join(sorted(BLOCKERS)))
    return errors


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--root', type=Path, default=ROOT,
                        help='repository root, for local audit only')
    parser.add_argument('--register', type=Path, default=None,
                        help='JSON register (default docs/production_risks.json)')
    parser.add_argument('--summary', action='store_true', help='print risk identifiers and their disposition')
    parser.add_argument('--strict-production', action='store_true',
                        help='intentionally refuse readiness while blockers exist')
    options = parser.parse_args(argv)
    root = options.root.resolve()
    path = options.register if options.register else root / 'docs/production_risks.json'
    try:
        data = json.loads(path.read_text(encoding='utf-8'))
    except (OSError, UnicodeError, json.JSONDecodeError) as exc:
        print(f'RISK AUDIT FAILED: cannot read valid JSON: {exc}', file=sys.stderr)
        return 1
    errors = validate_register(data, root)
    if errors:
        for error in errors:
            print(f'RISK AUDIT FAILED: {error}', file=sys.stderr)
        return 1
    risks = data['risks']
    blockers = [r for r in risks if r['status'] == 'blocker']
    if options.summary:
        for record in risks:
            print(f"{record['id']} {record['status']:20s} {record['title']}")
    print(f'RISK DOCUMENTATION VERIFIED: {len(risks)} risks, {len(blockers)} blockers; '
          'verdict NOT_PRODUCTION_READY')
    if options.strict_production:
        print('PRODUCTION READINESS REFUSED: unresolved blockers: ' +
              ', '.join(r['id'] for r in blockers), file=sys.stderr)
        return 3
    return 0


if __name__ == '__main__':
    sys.exit(main())
