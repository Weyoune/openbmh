#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 OpenBMH contributors
"""Synthetic checks for the source-distribution guard; no game input is used."""

import contextlib
import io
import json
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest
from unittest import mock

import check_public_tree as guard


class BlobChecks(unittest.TestCase):
    def rules(self, name, text):
        return {finding.rule for finding in guard.scan_blob(name, text.encode())}

    def test_legacy_names_without_english_word_false_positives(self):
        self.assertIn("legacy-address-symbol", self.rules("src/a.cpp", "FUN" + "_00012345();"))
        self.assertIn("legacy-address-symbol", self.rules("src/a.cpp", "raw_" + "ba384"))
        self.assertIn("legacy-address-symbol", self.rules("src/a.cpp", "has_source_" + "a2518"))
        self.assertNotIn("legacy-address-symbol", self.rules("src/a.cpp", "offer_row_added source_address"))

    def test_layout_constants_keep_suffixes_but_raw_calls_are_rejected(self):
        for suffix in ("", "U", "UL", "ULL", "lu"):
            with self.subTest(suffix=suffix):
                self.assertIn("unnamed-layout-address", self.rules("src/a.cpp", "read(0xa2345" + suffix + ");"))
                self.assertNotIn("unnamed-layout-address", self.rules("src/a.cpp", "inline constexpr auto kHeader = 0xa2345" + suffix + ";"))
        self.assertNotIn("unnamed-layout-address", self.rules("src/a.cpp", "mask(0x7ffff, 0x80000);"))

    def test_cpp_lexing_excludes_literal_contents(self):
        body = '\n'.join((
            'auto a = "// 0xa2345";',
            'auto b = R"tag(/* 0xa2345 */ " quoted )tag";',
            "auto n = 1'000; auto q = '\\'';",
            '// address 0xa2345UL',
            'read(0xa2345U);',
        ))
        findings = guard.scan_blob("src/a.cpp", body.encode())
        self.assertEqual({(f.rule, f.line) for f in findings},
                         {("address-in-comment", 4), ("unnamed-layout-address", 5)})

    def test_formats_may_document_layout_values(self):
        self.assertIn("address-outside-format-documentation", self.rules("README.md", "Field at 0xa2345"))
        self.assertNotIn("address-outside-format-documentation", self.rules("docs/formats/EXE.md", "Field at 0xa2345"))

    def test_relative_cmake_source_path_is_not_a_host_path(self):
        self.assertNotIn("private-provenance", self.rules("CMakeLists.txt", "${CMAKE_SOURCE_DIR}/tools/public_release/check_public_tree.py"))
        host = "/" + "tools/private/input"
        self.assertIn("private-provenance", self.rules("CMakeLists.txt", host))

    def test_original_asset_paths_and_binary_data_are_rejected(self):
        self.assertIn("binary-or-game-asset", self.rules("example.FBM", "synthetic"))
        self.assertIn("private-material-path", self.rules("tests/fixtures/input.txt", "synthetic"))
        self.assertIn("binary-content", self.rules("src/a.cpp", "synthetic\0bytes"))

    def test_vendor_attribution_is_preserved_without_exempting_secrets(self):
        email = "author" + "@example.org"
        for path in ("docs/licenses/vendor.txt", "tools/package/licenses/vendor.txt"):
            self.assertNotIn("personal-email", self.rules(path, email))
            self.assertIn("credential-pattern", self.rules(path, "ghp_" + "A" * 32))
        self.assertIn("personal-email", self.rules("README.md", email))

    def test_credential_assignments_exclude_cpp_identifier_expressions(self):
        fake = "A" * 32
        self.assertIn("credential-pattern", self.rules("settings.json", '{"token": "' + fake + '"}'))
        self.assertIn("credential-pattern", self.rules("settings.env", "API_TOKEN=" + fake))
        self.assertNotIn("credential-pattern", self.rules("src/a.cpp", "const int token = state.seller_team_token;"))


class ArchiveChecks(unittest.TestCase):
    def test_archive_is_checked_when_git_is_not_installed(self):
        with tempfile.TemporaryDirectory(prefix="openbmh-source-test-") as directory:
            root = Path(directory)
            (root / "README.md").write_text("Synthetic source\n", encoding="utf-8")
            (root / "bad.FBM").write_text("Synthetic refusal case\n", encoding="utf-8")
            with mock.patch.object(guard.subprocess, "run", side_effect=FileNotFoundError):
                findings, count = guard.scan_tree(root)
            self.assertEqual(count, 2)
            self.assertIn("binary-or-game-asset", {f.rule for f in findings})


@unittest.skipUnless(shutil.which("git"), "Git is required for repository-history tests")
class RepositoryChecks(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix="openbmh-guard-test-")
        self.root = Path(self.temp.name)
        self.git("init", "--initial-branch=main")
        self.git("config", "user.name", "Codex")
        self.git("config", "user.email", "codex@openbmh.invalid")

    def tearDown(self):
        self.temp.cleanup()

    def git(self, *args):
        return subprocess.run(["git", "-C", str(self.root), *args], check=True,
                              stdout=subprocess.PIPE, stderr=subprocess.PIPE)

    def commit(self, message):
        self.git("add", "--all")
        self.git("commit", "-m", message)

    def test_untracked_files_are_checked_and_build_outputs_ignored(self):
        (self.root / ".gitignore").write_text("build*/\n", encoding="utf-8")
        (self.root / "build-local").mkdir()
        (self.root / "build-local" / "cache.bin").write_bytes(b"\0")
        (self.root / "bad.FBM").write_text("synthetic", encoding="utf-8")
        findings, count = guard.scan_tree(self.root)
        self.assertEqual(count, 2)
        self.assertIn("binary-or-game-asset", {f.rule for f in findings})

    def test_deleted_asset_remains_rejected_in_history(self):
        asset = self.root / "old.FBM"
        asset.write_text("synthetic, never a game save", encoding="utf-8")
        self.commit("Add synthetic asset")
        asset.unlink()
        (self.root / "README.md").write_text("Clean tree\n", encoding="utf-8")
        self.commit("Remove synthetic asset")
        findings, _ = guard.scan_tree(self.root)
        self.assertEqual(findings, [])
        findings, commits, _ = guard.scan_history(self.root)
        self.assertEqual(commits, 2)
        self.assertIn("binary-or-game-asset", {f.rule for f in findings})

    def test_cli_reports_never_echo_credential_contents(self):
        credential = "ghp_" + "A" * 32
        (self.root / (credential + ".txt")).write_text(credential, encoding="utf-8")
        destination = self.root / "report.json"
        stdout = io.StringIO()
        with contextlib.redirect_stdout(stdout):
            code = guard.main(["--root", str(self.root), "--json", str(destination)])
        self.assertEqual(code, 1)
        self.assertNotIn(credential, stdout.getvalue())
        self.assertNotIn(credential, destination.read_text(encoding="utf-8"))
        self.assertFalse(json.loads(destination.read_text(encoding="utf-8"))["ok"])


if __name__ == "__main__":
    unittest.main()
