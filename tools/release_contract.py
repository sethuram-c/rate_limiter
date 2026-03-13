#!/usr/bin/env python3
"""Fail-closed check that VERSION, the installed header, and changelog agree.

This is source-contract validation, not a claim that the binary ABI is stable.
The C version test and staged-install consumer separately exercise the archive.
"""

from __future__ import annotations

import argparse
import pathlib
import re
import sys

SEMVER = re.compile(r"(?:0|[1-9][0-9]*)\.(?:0|[1-9][0-9]*)\.(?:0|[1-9][0-9]*)\Z")


class ContractError(ValueError):
    pass


def validate(root: pathlib.Path) -> str:
    version_path = root / "VERSION"
    header_path = root / "include" / "rate_version.h"
    changelog_path = root / "CHANGELOG.md"
    try:
        version = version_path.read_text(encoding="utf-8").strip()
        header = header_path.read_text(encoding="utf-8")
        changelog = changelog_path.read_text(encoding="utf-8")
    except (OSError, UnicodeError) as exc:
        raise ContractError(f"missing/unreadable release source: {exc}") from exc

    if not SEMVER.fullmatch(version):
        raise ContractError(f"VERSION must contain MAJOR.MINOR.PATCH: {version!r}")

    major, minor, patch = (int(value) for value in version.split("."))
    macros = {
        "RL_VERSION_MAJOR": str(major),
        "RL_VERSION_MINOR": str(minor),
        "RL_VERSION_PATCH": str(patch),
        "RL_VERSION_STRING": f'"{version}"',
    }
    for name, wanted in macros.items():
        # Exactly one literal definition must exist: don't silently accept a
        # duplicated macro or a mismatched string in the public header.
        matches = re.findall(rf"(?m)^#define[ \t]+{name}[ \t]+([^\r\n]+)$", header)
        if len(matches) != 1 or matches[0].strip() != wanted:
            raise ContractError(
                f"{name}: expected exactly one '#define {name} {wanted}', got {matches}"
            )

    if f"## [{version}] - Unreleased" not in changelog:
        raise ContractError(f"CHANGELOG.md needs an unreleased [{version}] entry")
    if major == 0 and ("ABI is not" not in changelog and "ABI is not stable" not in changelog):
        raise ContractError("pre-1.0 changelog must disclose ABI instability")
    return version


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=pathlib.Path,
                        default=pathlib.Path(__file__).resolve().parents[1])
    args = parser.parse_args()
    try:
        version = validate(args.root)
    except ContractError as exc:
        print(f"RELEASE CONTRACT FAILED: {exc}", file=sys.stderr)
        return 1
    print(f"release contract passed: {version} (Unreleased; ABI not guaranteed)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
