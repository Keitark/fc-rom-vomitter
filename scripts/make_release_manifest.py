#!/usr/bin/env python3
"""Generate or verify the public Rev A-FC SHA-256 manifest."""

from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
RELEASE = ROOT / "manufacturing" / "rev-a-fc"
MANIFEST = RELEASE / "release-manifest.json"
SOURCES = [
    ROOT / "hardware" / "nescart-fc.kicad_pcb",
    ROOT / "hardware" / "nescart-fc.kicad_pro",
    ROOT / "hardware" / "nescart-fc.kicad_sch",
]


def sha256(path: Path) -> str:
    value = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            value.update(block)
    return value.hexdigest().upper()


def build() -> dict:
    files = {}
    for path in sorted(RELEASE.rglob("*")):
        if not path.is_file() or path == MANIFEST:
            continue
        files[path.relative_to(RELEASE).as_posix()] = {
            "sha256": sha256(path),
            "bytes": path.stat().st_size,
        }
    return {
        "schema": "fc-rom-vomitter-public-release-v1",
        "revision": "Rev A-FC ROM Vomitter 2026-07-15",
        "validation_status": "fabrication-release-complete; hardware-bench-validation-pending",
        "source": {
            path.relative_to(ROOT).as_posix(): {
                "sha256": sha256(path),
                "bytes": path.stat().st_size,
            }
            for path in SOURCES
        },
        "gates": {
            "erc_errors": 0,
            "erc_reviewed_warnings": 8,
            "raw_unconnected_items": 0,
            "real_drc_errors": 0,
            "layout_check_failures": 0,
            "cpl_registration_unresolved": 0,
            "silk_unwaived_overlap": 0,
            "silk_unwaived_over_copper": 0,
            "intentional_silk_edge_findings": 2,
        },
        "board": {
            "layers": 6,
            "thickness_mm": 1.2,
            "maximum_envelope_mm": [90.0, 66.8],
            "controlled_impedance_option_required": False,
        },
        "files": files,
    }


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--check", action="store_true")
    args = parser.parse_args()
    expected = build()
    if args.check:
        actual = json.loads(MANIFEST.read_text(encoding="utf-8"))
        if actual != expected:
            raise SystemExit("release manifest is stale; run make_release_manifest.py")
        print(f"manifest PASS: {len(expected['files'])} release files")
        return 0
    MANIFEST.write_text(json.dumps(expected, indent=2) + "\n", encoding="utf-8")
    print(f"wrote {MANIFEST}: {len(expected['files'])} release files")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
