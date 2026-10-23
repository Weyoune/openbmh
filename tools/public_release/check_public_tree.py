#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 OpenBMH contributors
"""Check source distributions without printing matched credentials or file contents."""

from __future__ import annotations

import argparse
import dataclasses
import hashlib
import json
from pathlib import Path, PurePosixPath
import re
import shutil
import subprocess
import sys


@dataclasses.dataclass(frozen=True, order=True)
class Finding:
    path: str
    line: int
    rule: str


ASSET_SUFFIXES = {
    ".fbm", ".vga", ".blk", ".col", ".lbm", ".fnt", ".wap", ".ein", ".men", ".dat",
    ".exe", ".dll", ".so", ".dylib", ".wav", ".mp3", ".ogg", ".flac",
    ".avi", ".mov", ".mp4", ".png", ".jpg", ".jpeg", ".bmp", ".gif",
    ".bin", ".zip", ".7z", ".dmg", ".tar", ".gz", ".pdf", ".tga",
}
PRIVATE_PREFIXES = (
    ("data", "original"), ("docs", "manuals"), ("docs", "analyses"),
    ("docs", "agents"), ("src", "ui", "validation"),
    ("src", "ui", "reference"), ("src", "audio", "validation"),
    ("re",), ("tests", "fixtures"),
)
PRIVATE_DOCUMENTS = {
    "Status.md", "fltd.md", "BUILD_0.1_HANDOFF.md",
    "PUBLIC_RELEASE_TODO.md", "chat.txt",
}
SOURCE_SUFFIXES = {".cpp", ".hpp", ".h", ".c", ".inc"}
SKIP_DIRECTORIES = {".git", "__pycache__", ".cache", "_deps", "node_modules"}
# Exact independently generated artwork and synthetic native-editor captures.
# The latter are reviewed against docs/qa/editor-poc/README.md; no game material.
# Keep prior reviewed hashes if an asset is intentionally revised in history.
PROJECT_ARTWORK = {
    'docs/qa/editor-images/before-club-image.png': {
        'ac57bbbce2b8b7f71dad1d9e94d7b7baf182cb337d3800b5aac7a7f65db456bc',
    },
    'docs/qa/editor-images/linked-asset.png': {
        '76717df021e0c6db9a4afcab283923a20c2bf47d43f7e111303dca66041d3bf8',
    },
    'docs/qa/editor-images/preview-matchday-de.png': {
        'b7c0fad929fb28d25ef9e104e4ee90e62ab1d6e5ff8746bd0e8805bc2204d410',
    },
    'docs/qa/editor-images/preview-midnight-de.png': {
        'cc0ba1b7a1e4ea21f06fa9eb254a8b523da79bfa495a5068e345651619ab0407',
    },
    'docs/qa/editor-images/preview-silver-en.png': {
        '8704055293f58796b92057bc8a0c111b4db8bcaca297e45b7cb8d4bcacdd9a92',
    },
    "docs/qa/editor-regulations/before-competitions-0.5.1.png": {
        "cffa594e4d5ee942e086ab24fe7744a53dab4d5083dc1659f5e4b3ebdaee1f0b",
    },
    "docs/qa/editor-regulations/game-rules-midnight-de.png": {
        "b8778c35f4c94c327086198f765296698a401f4ef43029bb018e88c05fc7848f",
    },
    "docs/qa/editor-regulations/qualification-midnight-de.png": {
        "c1fa59f416db2ee56ac661b1c21101da645bc6d00ca2c1656438455a48b55295",
    },
    "docs/qa/editor-regulations/rules-matchday-de.png": {
        "45d09f05a7a4cf86bf465775f276c4f71790a053fa85a0e4bc42124d34702a34",
    },
    "docs/qa/editor-regulations/rules-midnight-de.png": {
        "f067fe69dd08c6f849fba3bae518727cc1252702494d077d2b7561c9aee3ed01",
    },
    "docs/qa/editor-regulations/rules-silver-en.png": {
        "e4e0f5269f23b4757b1bded384eb37857212236b626df7bc1352281f5eb5da1d",
    },
    "docs/qa/editor-comparison/before-players-0.5.0.png": {
        "e061cf1fcbafffc2950037f2f4bf24f28c00fe835988b56b198d88bce33b97e2",
    },
    "docs/qa/editor-comparison/players-midnight-de.png": {
        "ab1debb1e282d5462eab1a570137477884b2c773a900ec45a74783d50693367d",
    },
    "docs/qa/editor-comparison/teams-silver-en.png": {
        "ad8e8274f21465686b8c499ad44344ea4705cbec520560b12f8d7dde878204ea",
    },
    "docs/qa/editor-comparison/leagues-matchday-de.png": {
        "f24cd278b30ce3ccecc7320ae105ea5d0394e759c8045a76e75851259a487ada",
    },
    "docs/qa/editor-comparison/columns-midnight-de.png": {
        "74d825609b862c7dc60dc5182a32c010df73177b10d849079b02a2b4dc2d1620",
    },
    "docs/qa/editor-comparison/players-scroll-midnight-de.png": {
        "8e23a0fe6ea31ca6050fd5f157016948e0bad2f83093dcc3c53a3ba855c3bb40",
    },
    "docs/qa/editor-poc/clubs-midnight-de.png": {
        "2921aa67a870633c96f46328a82bc3022e5489b38d551c5314f13c7e59446070",
        "aa53e9cdd9ac8f6bf18b514862ac704ff9003bd1b62466cad2ebc329b09703dc",
        "ace9a5d491994592d4e87e8e1bce10af648a9c307645ace2f188c11d2b4f2fea",
    },
    "docs/qa/editor-poc/gui-search.png": {
        "48dc00d03c01d5bbec35683ff65672c12af4be6aecc8a84e5fece243c56643f4",
        "ef1780e91034dab96a7b23c638cdc8476f09be701b892f77e94e2fc2874cbd19",
    },
    "docs/qa/editor-poc/image-editor-midnight-de.png": {
        "44a89dfd84d7ee5b2acb54ae749e69051ae71ce192885dbe1a9311aba778c8ca",
        "eb01bd8fd8eaadfbc8e2b3f634bcdafbbb084dc4592d849e102a6f8c70ab0ad4",
    },
    "docs/qa/editor-poc/options-midnight-de.png": {
        "7075957db2d5f5a166bb71adb97d8208cb051a72b555202c212fae4f7e5c402c",
        "be483ad35a8912766611fd63dcc7cde6f820ed9a78441ad49554102a096633ab",
    },
    "docs/qa/editor-poc/players-silver-en.png": {
        "0d463d74a78072dda5b830c1d8c5e12d7e4588f6a743a95cc2ca4a327278928c",
        "d965a904663f3a30e5410f5d33539f6247737293625039fb9e3028337bbc088c",
    },
    "docs/qa/editor-poc/roster-matchday-de.png": {
        "547acb1b03ee68a3cee217fc5c96f4b960c64a5e48d1074e4f19045d98559647",
        "937829bb31b7452aa15d4a54cd03ad5de6a42d630b83cce279f22da09dfe180b",
    },
    "assets/launcher/stadium.tga": {
        "08f82783a357d5b0722f3737de54844150bc14d98036845f1aaee3211608e1c8",
        "fb8e5f1f86afe60de87b70a2f533690d1ef470241fa24ee4384ff000149edaab",
        "8bd89b7db8dbd0e92707240c5367cb92f8f32c2903e08157d0f278ac2599d6cf",
    },
    "docs/qa/editor-poc/clubs-silver-de.png": {
        "3e557f68bf8d0e8186dc237f9e92f6b9c7743d7df79fd29c3a5838ba01c116f2",
        "4f5dbe76b86097d306d2c982ce48093cce0fc95e9f35853493779b3116399239",
    },
    "docs/qa/editor-poc/options-silver-de.png": {
        "e406dca9058569f91fb314f0ff548ff349190c13ef1ed90c0051e44899634493",
    },
    "docs/qa/editor-poc/image-editor-silver-de.png": {
        "8d6ead6fd0e2fddc8e23de0a71abe519fa4f19ede0676823892d2568481bf68c",
    },
    "docs/qa/editor-poc/clubs-matchday-de.png": {
        "ddc5461068bdff4c0e3b468315cf03ef7cd203354941c586c030d3f3e80ae6ef",
        "e74d0b9acd07f6a48537616c28d74e53b8f69e20bb15fc3f6fcd0c10484b4f58",
    },
    "docs/qa/editor-poc/options-matchday-de.png": {
        "c0b2a8b936dccdcbac72169e2985ca91221ec605dd4ad96a624be23f225b608a",
    },
    "docs/qa/editor-poc/image-editor-matchday-de.png": {
        "e0f5fb9f986d01c27ce04da8bd8ac7623ee3b2afcbf8a8677c3580dcb0465c36",
    },
}

LEGACY_SYMBOLS = re.compile(
    r"(?<![A-Za-z0-9])(?:" + "FUN" + r"_?[0-9a-f]{4,8}|" + "DAT" +
    r"_[0-9a-f]{5,8}|source[_ ]*(?=[0-9a-f]*[0-9])[0-9a-f]{4,8}|"
    r"(?:raw|heap|saved|expected|sound_cfg)_(?=[ab][0-9a-f]*[0-9])[ab][0-9a-f]{4})(?![A-Za-z0-9])",
    re.IGNORECASE,
)
PRIVATE_TEXT = re.compile(
    r"(?:(?<![\w}./\\])/" + r"tools/|[A-Z]:[/\\]Users[/\\]|[A-Z]:[/\\]OpenBMH[/\\]|"
    r"docs/(?:analyses|agents)/|src/ui/(?:validation|reference)/|"
    + "bmh" + r"-work|matter" + r"most|\bMich" + r"ael\b|"
    r"(?<![\w}./\\])/" + r"root/[^\s]+|https?://(?:10\.|192\.168\.|172\.(?:1[6-9]|2[0-9]|3[01])\.)|"
    r"github\.com/[^/\s]+/openbmh[_-]dc\b)",
    re.IGNORECASE,
)
SECRET_PATTERNS = (
    re.compile(r"\bgh[pousr]_[A-Za-z0-9_]{20,}\b"),
    re.compile(r"\bgithub_pat_[A-Za-z0-9_]{30,}\b"),
    re.compile(r"-----BEGIN (?:[A-Z0-9]+ )?PRIVATE KEY-----"),
    re.compile(r"\bAKIA[0-9A-Z]{16}\b"),
    re.compile(r"(?i)\bBearer\s+[A-Za-z0-9._~+/-]{20,}"),
    re.compile(r"(?i)\b[A-Z_]*(?:TOKEN|PASSWORD|SECRET|API_KEY)['\"]?\s*[:=]\s*['\"][A-Za-z0-9_./+-]{20,}['\"]"),
    re.compile(r"(?m)^(?:export\s+)?[A-Z_]*(?:TOKEN|PASSWORD|SECRET|API_KEY)\s*=\s*[A-Za-z0-9_./+-]{20,}(?:\s|$)"),
)
EMAIL = re.compile(r"\b[A-Z0-9._%+-]+@[A-Z0-9.-]+\.[A-Z]{2,}\b", re.IGNORECASE)
# Resolution-qualified artwork names are filenames, including in older commits.
SIZED_ASSET_NAME = re.compile(r"[A-Z0-9._+-]+@[1-9][0-9]{0,4}\.(?:png|jpe?g|webp|svg|gif|avif)", re.IGNORECASE)
# Project bot identities and the co-author trailer address are not personal data.
BOT_EMAILS = {"codex@openbmh.invalid", "claude@openbmh.invalid", "noreply@anthropic.com"}
HEX_NUMBER = re.compile(r"\b0x(?P<digits>[0-9a-f]{5,8})(?:u(?:ll?|z)?|ll?u?|z)?\b", re.IGNORECASE)
LAYOUT_BEGIN = 0x50000
LAYOUT_END = 0xE0000


def line_at(text: str, position: int) -> int:
    return text.count("\n", 0, position) + 1


def cpp_exclusions(text: str):
    """Yield comment/literal spans, respecting raw strings and numeric separators."""
    at = 0
    while at < len(text):
        if text.startswith("//", at):
            end = text.find("\n", at)
            end = len(text) if end < 0 else end
            yield at, end, "comment"
            at = end
        elif text.startswith("/*", at):
            end = text.find("*/", at + 2)
            end = len(text) if end < 0 else end + 2
            yield at, end, "comment"
            at = end
        elif text.startswith('R"', at):
            opening = text.find("(", at + 2, at + 19)
            if opening < 0:
                at += 2
                continue
            delimiter = text[at + 2:opening]
            end = text.find(")" + delimiter + '"', opening + 1)
            finish = len(text) if end < 0 else end + len(delimiter) + 2
            yield at, finish, "literal"
            at = finish
        elif text[at] == "'" and at + 1 < len(text) and text[at + 1].isalnum() and re.search(
                r"\b[0-9][a-fA-F0-9xXbB']*$", text[max(0, at - 200):at]):
            at += 1  # C++ numeric separator, rather than a character literal.
        elif text[at] in "\"'":
            start = at
            quote = text[at]
            at += 1
            while at < len(text):
                if text[at] == "\\":
                    at += 2
                elif text[at] == quote:
                    at += 1
                    break
                else:
                    at += 1
            yield start, at, "literal"
        else:
            at += 1


def is_layout_value(value: int) -> bool:
    return LAYOUT_BEGIN <= value < LAYOUT_END and value & (value - 1) != 0 and value & (value + 1) != 0


def layout_findings(name: str, text: str):
    exclusions = list(cpp_exclusions(text))
    code = list(text)
    for start, end, kind in exclusions:
        if kind == "comment":
            for match in HEX_NUMBER.finditer(text, start, end):
                if is_layout_value(int(match.group("digits"), 16)):
                    yield Finding(name, line_at(text, match.start()), "address-in-comment")
        for index in range(start, end):
            if code[index] != "\n":
                code[index] = " "
    code = "".join(code)
    for match in HEX_NUMBER.finditer(code):
        if not is_layout_value(int(match.group("digits"), 16)):
            continue
        # Keep numeric format knowledge in a named constant, preserving its type.
        begin = max(code.rfind(";", 0, match.start()), code.rfind("{", 0, match.start()),
                    code.rfind("}", 0, match.start())) + 1
        prefix = code[begin:match.start()]
        declaration = re.search(r"\b(?:constexpr|constinit|const)\b[^;{}]*\b[A-Za-z_]\w*\s*=\s*\(?\s*$", prefix)
        if declaration is None:
            yield Finding(name, line_at(text, match.start()), "unnamed-layout-address")


def path_findings(name: str) -> list[Finding]:
    path = PurePosixPath(name)
    parts = path.parts
    result = []
    if path.is_absolute() or ".." in parts:
        result.append(Finding(name, 0, "unsafe-path"))
    if path.suffix.lower() in ASSET_SUFFIXES and name not in PROJECT_ARTWORK:
        result.append(Finding(name, 0, "binary-or-game-asset"))
    if path.name in PRIVATE_DOCUMENTS or any(parts[:len(p)] == p for p in PRIVATE_PREFIXES):
        result.append(Finding(name, 0, "private-material-path"))
    if any(p.startswith(".claude") or p.startswith(".codex") for p in parts):
        result.append(Finding(name, 0, "private-agent-configuration"))
    return result


def scan_blob(name: str, blob: bytes) -> list[Finding]:
    result = path_findings(name)
    if name in PROJECT_ARTWORK:
        if hashlib.sha256(blob).hexdigest() not in PROJECT_ARTWORK[name]:
            result.append(Finding(name, 0, "unreviewed-project-artwork"))
        return result
    if len(blob) > 2_000_000:
        result.append(Finding(name, 0, "oversized-source-file"))
    if b"\0" in blob:
        result.append(Finding(name, 0, "binary-content"))
        return result
    try:
        text = blob.decode("utf-8-sig")
    except UnicodeDecodeError:
        result.append(Finding(name, 0, "non-utf8-source"))
        return result
    for pattern in SECRET_PATTERNS:
        for match in pattern.finditer(text):
            result.append(Finding(name, line_at(text, match.start()), "credential-pattern"))
    # These exact upstream license/copyright documents must remain unmodified.
    upstream_notice = name.startswith(("docs/licenses/", "tools/package/licenses/",
                                       "src/audio/third_party/"))
    if upstream_notice:
        return result
    for pattern, rule in [(LEGACY_SYMBOLS, "legacy-address-symbol"), (PRIVATE_TEXT, "private-provenance")]:
        for match in pattern.finditer(text):
            result.append(Finding(name, line_at(text, match.start()), rule))
    for match in EMAIL.finditer(text):
        if match.group().lower() not in BOT_EMAILS and not SIZED_ASSET_NAME.fullmatch(match.group()):
            result.append(Finding(name, line_at(text, match.start()), "personal-email"))
    if PurePosixPath(name).suffix.lower() in SOURCE_SUFFIXES:
        result.extend(layout_findings(name, text))
    elif PurePosixPath(name).suffix.lower() == ".md" and not name.startswith("docs/formats/"):
        for match in HEX_NUMBER.finditer(text):
            if is_layout_value(int(match.group("digits"), 16)):
                result.append(Finding(name, line_at(text, match.start()), "address-outside-format-documentation"))
    return result


def checked_files(root: Path):
    """Use the Git index when available; otherwise inspect a plain source archive."""
    try:
        git = subprocess.run(["git", "-C", str(root), "rev-parse", "--show-toplevel"],
                             capture_output=True, text=True)
    except FileNotFoundError:
        git = None
    if git is not None and git.returncode == 0 and Path(git.stdout.strip()).resolve() == root:
        output = subprocess.check_output(["git", "-C", str(root), "ls-files", "--cached",
                                          "--others", "--exclude-standard", "-z"])
        return sorted({p.decode("utf-8") for p in output.split(b"\0") if p})
    return sorted(str(p.relative_to(root).as_posix()) for p in root.rglob("*")
                  if p.is_file() and not any(part in SKIP_DIRECTORIES or part.startswith("build")
                                             for part in p.relative_to(root).parts))


def scan_tree(root: Path):
    findings = []
    names = checked_files(root)
    for name in names:
        path = root / name
        if path.is_symlink():
            findings.append(Finding(name, 0, "symlink-not-reviewed"))
        elif not path.is_file():
            findings.append(Finding(name, 0, "missing-tracked-file"))
        else:
            findings.extend(scan_blob(name, path.read_bytes()))
    return findings, len(names)


def scan_history(root: Path, revision=None):
    findings = []
    references = subprocess.check_output(["git", "-C", str(root), "for-each-ref", "--format=%(refname)"])
    findings.extend(scan_blob("ref-names", references))
    selection = [revision] if revision else ["--all"]
    refs = subprocess.check_output(["git", "-C", str(root), "rev-list", *selection], text=True).splitlines()
    seen = set()
    checked = 0
    for revision in refs:
        entries = subprocess.check_output(["git", "-C", str(root), "ls-tree", "-rz", revision])
        for entry in entries.split(b"\0"):
            if not entry:
                continue
            metadata, name = entry.split(b"\t", 1)
            mode, kind, object_id = metadata.decode().split()
            path = name.decode("utf-8")
            if kind != "blob":
                findings.append(Finding(path, 0, "unreviewed-history-object"))
                continue
            if mode == "120000":
                findings.append(Finding(path, 0, "symlink-in-history"))
            key = (object_id, path)
            if key in seen:
                continue
            seen.add(key)
            data = subprocess.check_output(["git", "-C", str(root), "cat-file", "blob", object_id])
            findings.extend(scan_blob(path, data))
            checked += 1
        message = subprocess.check_output(["git", "-C", str(root), "show", "-s", "--format=%B", revision])
        findings.extend(scan_blob("commit-message", message))
    return findings, len(refs), checked


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, default=Path.cwd())
    parser.add_argument("--history", action="store_true", help="also check all reachable commits and their blobs")
    parser.add_argument("--revision", help="with --history, inspect this revision's ancestry instead of unrelated refs")
    parser.add_argument("--json", type=Path, help="write a report containing rule names and locations only")
    args = parser.parse_args(argv)
    if args.revision and (not args.history or args.revision.startswith('-')):
        parser.error("--revision requires --history and a revision, not an option")
    if args.history and shutil.which("git") is None:
        parser.error("Git is required for history checks")
    root = args.root.resolve(strict=True)
    findings, files = scan_tree(root)
    commits = blobs = 0
    if args.history:
        extra, commits, blobs = scan_history(root, args.revision)
        findings.extend(extra)
    findings = sorted(set(findings))
    def display_path(name):
        for pattern in SECRET_PATTERNS:
            name = pattern.sub("[redacted]", name)
        return name
    report = {"ok": not findings, "files_checked": files, "history_checked": args.history,
              "commits_checked": commits, "history_blobs_checked": blobs,
              "findings": [{**dataclasses.asdict(f), "path": display_path(f.path)} for f in findings]}
    if args.json:
        args.json.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    for finding in findings:
        print(f"{display_path(finding.path)}:{finding.line}: {finding.rule}")
    print(f"Public-source check: {files} files, {commits} commits, {len(findings)} findings")
    return int(bool(findings))


if __name__ == "__main__":
    sys.exit(main())
