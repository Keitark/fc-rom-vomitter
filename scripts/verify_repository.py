#!/usr/bin/env python3
"""Privacy, scope, link and release-integrity checks for the public repo."""

from __future__ import annotations

import re
import subprocess
import sys
from pathlib import Path
from urllib.parse import unquote


ROOT = Path(__file__).resolve().parents[1]
TEXT_SUFFIXES = {
    ".c", ".h", ".md", ".py", ".ps1", ".txt", ".csv", ".json",
    ".ini", ".yml", ".yaml", ".kicad_pcb", ".kicad_pro", ".kicad_sch",
    ".kicad_sym", ".kicad_mod",
}
BANNED_PARTS = {"__pycache__", ".pio", ".codex", ".claude", "orders", "private", "screenshots", "states"}
BANNED_PATTERNS = {
    "wrong account": re.compile(r"kkondoun", re.I),
    "browser cart token": re.compile(r"cartaccessid|pcbfileno", re.I),
    "JLCPCB batch/order id": re.compile(r"\b(?:W20\d{14}|SO\d{10,}|SMT\d{10,})\b"),
    "GitHub token": re.compile(r"\b(?:gho_|ghp_|github_pat_)[A-Za-z0-9_]+"),
    "private key": re.compile(r"BEGIN [A-Z ]*PRIVATE KEY"),
    "absolute user path": re.compile(r"[A-Za-z]:\\Users\\", re.I),
}
LINK = re.compile(r"\[[^\]]*\]\(([^)]+)\)")


def files() -> list[Path]:
    # Match the files that could actually be committed. Local PlatformIO,
    # CMake and Python build products are ignored and may contain machine paths.
    proc = subprocess.run(
        ["git", "ls-files", "-z", "--cached", "--others", "--exclude-standard"],
        cwd=ROOT,
        check=True,
        capture_output=True,
    )
    return [ROOT / raw.decode("utf-8") for raw in proc.stdout.split(b"\0") if raw]


def check_scope(paths: list[Path]) -> list[str]:
    failures = []
    allowed_top = {
        ".github", "LICENSES", "docs", "experimental", "firmware", "hardware", "manufacturing", "scripts",
        ".gitattributes", ".gitignore", "AGENTS.md", "CONTRIBUTING.md", "LICENSE",
        "README.md", "README_JA.md", "SECURITY.md",
    }
    for path in paths:
        rel = path.relative_to(ROOT)
        if rel.parts[0] not in allowed_top:
            failures.append(f"unexpected top-level path: {rel}")
        if BANNED_PARTS.intersection(rel.parts):
            failures.append(f"banned path: {rel}")
    return failures


def check_text(paths: list[Path]) -> list[str]:
    failures = []
    for path in paths:
        if path.suffix.lower() not in TEXT_SUFFIXES and path.name not in {"LICENSE", ".gitignore", ".gitattributes"}:
            continue
        text = path.read_text(encoding="utf-8", errors="replace")
        rel = path.relative_to(ROOT)
        # The validator necessarily contains its own forbidden signatures.
        if rel.as_posix() != "scripts/verify_repository.py":
            for label, pattern in BANNED_PATTERNS.items():
                if pattern.search(text):
                    failures.append(f"{label}: {rel}")
        if path.suffix.lower() == ".md":
            for raw in LINK.findall(text):
                target = raw.strip().split("#", 1)[0]
                if not target or target.startswith(("http://", "https://", "mailto:")):
                    continue
                candidate = (path.parent / unquote(target)).resolve()
                try:
                    candidate.relative_to(ROOT)
                except ValueError:
                    failures.append(f"link escapes repository: {rel} -> {raw}")
                    continue
                if not candidate.exists():
                    failures.append(f"broken relative link: {rel} -> {raw}")
    return failures


def main() -> int:
    paths = files()
    failures = check_scope(paths) + check_text(paths)
    proc = subprocess.run(
        [sys.executable, str(ROOT / "scripts" / "make_release_manifest.py"), "--check"],
        cwd=ROOT,
    )
    if proc.returncode:
        failures.append("release manifest check failed")
    if failures:
        print("repository validation FAILED")
        for failure in sorted(set(failures)):
            print(f"- {failure}")
        return 1
    print(f"repository validation PASS: {len(paths)} files")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
