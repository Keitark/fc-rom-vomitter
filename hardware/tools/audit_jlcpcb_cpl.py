"""Audit JLCPCB CPL origins/rotations against EasyEDA package pad geometry.

Run with KiCad's bundled Python.  The script compares the actual KiCad pad
centres with the official EasyEDA/JLC package for every populated LCSC part,
finds the rigid 0/90/180/270-degree placement with the lowest residual, and
writes a corrected CPL plus a CSV audit report.
"""

from __future__ import annotations

import argparse
import csv
import json
import math
import re
import urllib.request
from collections import Counter, defaultdict
from pathlib import Path

import pcbnew


EASYEDA_UNIT_MM = 0.254


def parse_args() -> argparse.Namespace:
    root = Path(__file__).resolve().parents[2]
    order = root / "hardware" / "orders" / "jlcpcb-quote-20260711"
    parser = argparse.ArgumentParser()
    parser.add_argument("--board", type=Path, default=root / "hardware" / "nescart.kicad_pcb")
    parser.add_argument("--bom", type=Path, default=order / "02_nescart-BOM.csv")
    parser.add_argument("--cpl", type=Path, default=order / "03_nescart-CPL.csv")
    parser.add_argument("--output", type=Path, default=order / "03_nescart-CPL-corrected.csv")
    parser.add_argument("--report", type=Path, default=order / "05_cpl-registration-audit.csv")
    parser.add_argument(
        "--overrides",
        type=Path,
        help=("CSV of visually verified package registrations. Expected columns: "
              "Designator, New X, New Y, New Rotation, and optional Evidence."),
    )
    parser.add_argument(
        "--package-cache",
        type=Path,
        default=root / "hardware" / "tmp" / "easyeda-package-cache",
        help="Cache official EasyEDA package JSON to make repeat audits deterministic.",
    )
    return parser.parse_args()


def load_overrides(path: Path | None) -> dict[str, tuple[float, float, int, str]]:
    if path is None:
        return {}
    with path.open(newline="", encoding="utf-8-sig") as handle:
        rows = list(csv.DictReader(handle))
    overrides: dict[str, tuple[float, float, int, str]] = {}
    for row in rows:
        ref = row.get("Designator", "").strip()
        if not ref:
            raise RuntimeError(f"Blank Designator in override file {path}")
        if ref in overrides:
            raise RuntimeError(f"Duplicate override for {ref} in {path}")
        try:
            x = float(row["New X"])
            y = float(row["New Y"])
            rotation = round(float(row["New Rotation"])) % 360
        except (KeyError, TypeError, ValueError) as exc:
            raise RuntimeError(f"Invalid override row for {ref} in {path}: {row}") from exc
        overrides[ref] = (x, y, rotation, row.get("Evidence", "").strip())
    return overrides


def fetch_package(lcsc: str, cache_dir: Path | None = None) -> dict:
    cache_path = cache_dir / f"{lcsc}.json" if cache_dir else None
    if cache_path and cache_path.exists():
        return json.loads(cache_path.read_text(encoding="utf-8"))
    url = f"https://easyeda.com/api/products/{lcsc}/components?version=6.4.19.5"
    req = urllib.request.Request(url, headers={
        "User-Agent": ("Mozilla/5.0 (Windows NT 10.0; Win64; x64) "
                       "AppleWebKit/537.36 Chrome/138.0 Safari/537.36"),
        "Accept": "application/json, text/plain, */*",
        "Referer": "https://easyeda.com/",
    })
    with urllib.request.urlopen(req, timeout=30) as response:
        data = json.load(response)
    if not data.get("success") or not data.get("result", {}).get("packageDetail"):
        raise RuntimeError(f"No EasyEDA package data for {lcsc}")
    package = data["result"]["packageDetail"]["dataStr"]
    if cache_path:
        cache_path.parent.mkdir(parents=True, exist_ok=True)
        cache_path.write_text(json.dumps(package, ensure_ascii=False, indent=2), encoding="utf-8")
    return package


def expand_pad_name(name: str) -> list[str]:
    if re.fullmatch(r"[A-Za-z]+\d+(?:[A-Za-z]+\d+)+", name):
        return re.findall(r"[A-Za-z]+\d+", name)
    return [name]


def easyeda_pad_centres(package: dict) -> dict[str, tuple[float, float]]:
    head = package["head"]
    ox, oy = float(head["x"]), float(head["y"])
    grouped: dict[str, list[tuple[float, float]]] = defaultdict(list)
    for shape in package.get("shape", []):
        fields = shape.split("~")
        if not fields or fields[0] != "PAD" or len(fields) < 9:
            continue
        x, y = float(fields[2]), float(fields[3])
        name = fields[8]
        if not name:
            continue
        point = ((x - ox) * EASYEDA_UNIT_MM, (y - oy) * EASYEDA_UNIT_MM)
        for expanded in expand_pad_name(name):
            grouped[expanded].append(point)
    return {
        name: (
            sum(point[0] for point in points) / len(points),
            sum(point[1] for point in points) / len(points),
        )
        for name, points in grouped.items()
    }


def kicad_pad_centres(footprint) -> dict[str, tuple[float, float]]:
    origin = footprint.GetPosition()
    grouped: dict[str, list[tuple[float, float]]] = defaultdict(list)
    for pad in footprint.Pads():
        name = pad.GetNumber()
        if not name:
            continue
        point = pad.GetPosition()
        grouped[name].append(((point.x - origin.x) / 1e6, (point.y - origin.y) / 1e6))
    return {
        name: (
            sum(point[0] for point in points) / len(points),
            sum(point[1] for point in points) / len(points),
        )
        for name, points in grouped.items()
    }


def easyeda_pad_name_counts(package: dict) -> Counter[str]:
    counts: Counter[str] = Counter()
    for shape in package.get("shape", []):
        fields = shape.split("~")
        if not fields or fields[0] != "PAD" or len(fields) < 9 or not fields[8]:
            continue
        for expanded in expand_pad_name(fields[8]):
            counts[expanded] += 1
    return counts


def kicad_pad_name_counts(footprint) -> Counter[str]:
    return Counter(pad.GetNumber() for pad in footprint.Pads() if pad.GetNumber())


def pin_map_issues(kicad_counts: Counter[str], easyeda_counts: Counter[str]) -> list[str]:
    issues = []
    kicad_only = sorted(set(kicad_counts) - set(easyeda_counts))
    easyeda_only = sorted(set(easyeda_counts) - set(kicad_counts))
    count_mismatch = {
        name: (kicad_counts[name], easyeda_counts[name])
        for name in sorted(set(kicad_counts) & set(easyeda_counts))
        if kicad_counts[name] != easyeda_counts[name]
    }
    if kicad_only:
        issues.append(f"KiCad-only pad names {kicad_only}")
    if easyeda_only:
        issues.append(f"EasyEDA-only pad names {easyeda_only}")
    if count_mismatch:
        issues.append(f"physical pad-count mismatch {count_mismatch}")
    return issues


def rotate_screen(point: tuple[float, float], degrees: int) -> tuple[float, float]:
    # Positive JLC/EasyEDA rotations are counter-clockwise in Cartesian
    # coordinates; in screen/PCB coordinates (+Y down), this matrix applies.
    radians = math.radians(degrees)
    x, y = point
    return (x * math.cos(radians) + y * math.sin(radians),
            -x * math.sin(radians) + y * math.cos(radians))


def solve_registration(kicad: dict[str, tuple[float, float]], easyeda: dict[str, tuple[float, float]]):
    common = sorted(set(kicad) & set(easyeda))
    if len(common) < 2:
        raise RuntimeError(f"Only {len(common)} common pads: {common}")
    best = None
    for angle in (0, 90, 180, 270):
        deltas = []
        for name in common:
            ex, ey = rotate_screen(easyeda[name], angle)
            kx, ky = kicad[name]
            deltas.append((kx - ex, ky - ey))
        dx = sum(item[0] for item in deltas) / len(deltas)
        dy = sum(item[1] for item in deltas) / len(deltas)
        residual = math.sqrt(sum((x - dx) ** 2 + (y - dy) ** 2 for x, y in deltas) / len(deltas))
        candidate = (residual, angle, dx, dy, len(common))
        if best is None or candidate < best:
            best = candidate
    return best


def main() -> int:
    args = parse_args()
    board = pcbnew.LoadBoard(str(args.board))
    footprints = {fp.GetReference(): fp for fp in board.GetFootprints()}

    with args.bom.open(newline="", encoding="utf-8-sig") as handle:
        bom_rows = list(csv.DictReader(handle))
    with args.cpl.open(newline="", encoding="utf-8-sig") as handle:
        cpl_rows = list(csv.DictReader(handle))
    cpl_by_ref = {row["Designator"]: row for row in cpl_rows}
    overrides = load_overrides(args.overrides)
    unknown_overrides = sorted(set(overrides) - set(cpl_by_ref))
    if unknown_overrides:
        raise RuntimeError(f"Overrides reference parts absent from CPL: {unknown_overrides}")

    report_rows = []
    corrections: dict[str, tuple[float, float, int]] = {}
    package_cache = {}
    for bom in bom_rows:
        lcsc = bom["LCSC Part #"].strip()
        refs = [item.strip() for item in bom["Designator"].split(",") if item.strip()]
        if not lcsc or not refs:
            continue
        if lcsc not in package_cache:
            try:
                package_cache[lcsc] = fetch_package(lcsc, args.package_cache)
            except RuntimeError:
                # A documented per-reference override may be used when the
                # exact supplier part has no EasyEDA package data.  Do not
                # generalize this to an unreviewed package family: every
                # reference on the affected BOM line must have evidence.
                if not all(ref in overrides for ref in refs):
                    raise
                package_cache[lcsc] = None
        package = package_cache[lcsc]
        easyeda = easyeda_pad_centres(package) if package is not None else {}

        for ref in refs:
            if ref not in footprints or ref not in cpl_by_ref:
                continue
            fp = footprints[ref]
            # Solve each placed footprint independently.  A single BOM line can
            # contain references at different rotations, so reusing one
            # representative's transform would apply the wrong absolute angle
            # and origin offset to the other references.
            kicad = kicad_pad_centres(fp)
            kicad_counts = kicad_pad_name_counts(fp)
            easyeda_counts = (easyeda_pad_name_counts(package)
                              if package is not None else Counter())
            mapping_issues = pin_map_issues(kicad_counts, easyeda_counts)
            candidate_x = candidate_y = math.nan
            candidate_rotation = math.nan
            try:
                if package is None:
                    raise RuntimeError("supplier package geometry unavailable; evidence override required")
                residual, angle, dx, dy, matched = solve_registration(kicad, easyeda)
                position = fp.GetPosition()
                candidate_x = position.x / 1e6 + dx
                candidate_board_y = position.y / 1e6 + dy
                candidate_y = -candidate_board_y
                candidate_rotation = int(angle)
                status = "PASS" if residual <= 0.10 else "FAIL_GEOMETRY"
                error = ("" if status == "PASS" else
                         f"Best RMS residual {residual:.4f} mm exceeds 0.1000 mm")
            except Exception as exc:
                residual, angle, dx, dy, matched = math.nan, math.nan, math.nan, math.nan, 0
                status, error = "UNRESOLVED", str(exc)

            if mapping_issues and status == "PASS":
                status = "FAIL_PIN_MAP"
                error = "; ".join(mapping_issues)
            elif mapping_issues and error:
                error = f"{error}; {'; '.join(mapping_issues)}"

            override = overrides.get(ref)
            override_evidence = ""
            if override:
                desired_x, desired_cpl_y, desired_rotation, override_evidence = override
                status = "PASS_OVERRIDE"
                error = ""
                corrections[ref] = (desired_x, desired_cpl_y, desired_rotation)
            elif status == "PASS":
                desired_x = candidate_x
                desired_cpl_y = candidate_y
                desired_rotation = candidate_rotation
                corrections[ref] = (desired_x, desired_cpl_y, int(desired_rotation))

            if status in {"PASS", "PASS_OVERRIDE"}:
                current = cpl_by_ref[ref]
                delta_x = desired_x - float(current["Mid X"])
                delta_y = desired_cpl_y - float(current["Mid Y"])
                delta_rotation = (int(desired_rotation) - round(float(current["Rotation"]))) % 360
            else:
                desired_x = desired_cpl_y = desired_rotation = math.nan
                delta_x = delta_y = delta_rotation = math.nan
            report_rows.append({
                "Designator": ref,
                "LCSC Part #": lcsc,
                "Status": status,
                "Matched Pads": matched,
                "KiCad Physical Pads": sum(kicad_counts.values()),
                "EasyEDA Physical Pads": sum(easyeda_counts.values()),
                "Pin-map Issues": "; ".join(mapping_issues),
                "RMS Residual mm": f"{residual:.4f}" if math.isfinite(residual) else "",
                "Current X": cpl_by_ref[ref]["Mid X"],
                "Current Y": cpl_by_ref[ref]["Mid Y"],
                "Current Rotation": cpl_by_ref[ref]["Rotation"],
                "Candidate X": f"{candidate_x:.4f}" if math.isfinite(candidate_x) else "",
                "Candidate Y": f"{candidate_y:.4f}" if math.isfinite(candidate_y) else "",
                "Candidate Rotation": str(int(candidate_rotation)) if math.isfinite(candidate_rotation) else "",
                "Corrected X": f"{desired_x:.4f}" if math.isfinite(desired_x) else "",
                "Corrected Y": f"{desired_cpl_y:.4f}" if math.isfinite(desired_cpl_y) else "",
                "Corrected Rotation": str(int(desired_rotation)) if math.isfinite(desired_rotation) else "",
                "Delta X mm": f"{delta_x:+.4f}" if math.isfinite(delta_x) else "",
                "Delta Y mm": f"{delta_y:+.4f}" if math.isfinite(delta_y) else "",
                "Delta Rotation": str(delta_rotation) if math.isfinite(delta_rotation) else "",
                "Override Evidence": override_evidence,
                "Error": error,
            })

    corrected_rows = []
    for row in cpl_rows:
        updated = dict(row)
        correction = corrections.get(row["Designator"])
        if correction:
            updated["Mid X"] = f"{correction[0]:.4f}"
            updated["Mid Y"] = f"{correction[1]:.4f}"
            updated["Rotation"] = str(correction[2])
        corrected_rows.append(updated)

    args.report.parent.mkdir(parents=True, exist_ok=True)
    with args.report.open("w", newline="", encoding="utf-8") as handle:
        writer = csv.DictWriter(handle, fieldnames=list(report_rows[0]))
        writer.writeheader()
        writer.writerows(report_rows)

    failures = [row for row in report_rows if row["Status"] not in {"PASS", "PASS_OVERRIDE"}]
    changed = [row for row in report_rows if row["Status"] in {"PASS", "PASS_OVERRIDE"} and
               (abs(float(row["Delta X mm"])) > 0.02 or abs(float(row["Delta Y mm"])) > 0.02 or
                int(row["Delta Rotation"]) != 0)]
    print(f"Audited {len(report_rows)} populated references; {len(failures)} unresolved; {len(changed)} corrected")
    for row in changed:
        print(row["Designator"], row["LCSC Part #"], row["Delta X mm"], row["Delta Y mm"], row["Delta Rotation"],
              "residual", row["RMS Residual mm"])
    for row in failures:
        print(row["Status"], row["Designator"], row["LCSC Part #"], row["Error"])
    if failures:
        if args.output.exists():
            args.output.unlink()
        print(f"Blocked upload CPL output because {len(failures)} references remain unresolved")
        return 1

    args.output.parent.mkdir(parents=True, exist_ok=True)
    with args.output.open("w", newline="", encoding="utf-8") as handle:
        writer = csv.DictWriter(handle, fieldnames=["Designator", "Mid X", "Mid Y", "Layer", "Rotation"])
        writer.writeheader()
        writer.writerows(corrected_rows)
    print(f"Wrote uploadable CPL to {args.output}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
