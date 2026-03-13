#!/usr/bin/env python3
"""Mutation/negative tests for release-contract validation."""

from __future__ import annotations

import pathlib
import sys
import tempfile
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
from release_contract import ContractError, validate  # noqa: E402


class ReleaseContractTests(unittest.TestCase):
    def setUp(self):
        self.directory = tempfile.TemporaryDirectory()
        self.addCleanup(self.directory.cleanup)
        self.root = pathlib.Path(self.directory.name)
        (self.root / "include").mkdir()
        for rel in ("VERSION", "CHANGELOG.md", "include/rate_version.h"):
            dst = self.root / rel
            dst.write_bytes((ROOT / rel).read_bytes())

    def test_reference_snapshot(self):
        self.assertEqual(validate(self.root), "0.1.0")

    def test_header_version_mismatch_rejected(self):
        header = self.root / "include/rate_version.h"
        header.write_text(header.read_text().replace('#define RL_VERSION_PATCH 0',
                                                      '#define RL_VERSION_PATCH 1'))
        with self.assertRaisesRegex(ContractError, 'RL_VERSION_PATCH'):
            validate(self.root)

    def test_duplicate_definitions_rejected(self):
        header = self.root / "include/rate_version.h"
        header.write_text(header.read_text() + '\n#define RL_VERSION_MAJOR 0\n')
        with self.assertRaisesRegex(ContractError, 'RL_VERSION_MAJOR'):
            validate(self.root)

    def test_invalid_semver_rejected(self):
        (self.root / "VERSION").write_text("v0.1.0\n")
        with self.assertRaisesRegex(ContractError, 'VERSION'):
            validate(self.root)

    def test_missing_changelog_entry_rejected(self):
        (self.root / "CHANGELOG.md").write_text("# Changelog\n")
        with self.assertRaisesRegex(ContractError, 'CHANGELOG'):
            validate(self.root)

    def test_missing_version_file_rejected(self):
        (self.root / "VERSION").unlink()
        with self.assertRaisesRegex(ContractError, 'release source'):
            validate(self.root)

    def test_missing_pre1_abi_warning_rejected(self):
        changelog = self.root / "CHANGELOG.md"
        changelog.write_text(changelog.read_text().replace('ABI is not\n  stable', 'ABI is guaranteed stable'))
        with self.assertRaisesRegex(ContractError, 'ABI instability'):
            validate(self.root)


if __name__ == "__main__":
    unittest.main()
