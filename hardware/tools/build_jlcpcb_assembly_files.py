"""Build and reconcile JLCPCB BOM/CPL files from released FC artifacts."""

from __future__ import annotations

import argparse
import csv
from pathlib import Path


def refs(value: str) -> list[str]:
    return [item.strip() for item in value.split(",") if item.strip()]


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--positions", required=True, type=Path)
    parser.add_argument("--sourcing-lock", required=True, type=Path)
    parser.add_argument("--bom-output", required=True, type=Path)
    parser.add_argument("--cpl-output", required=True, type=Path)
    args = parser.parse_args()

    with args.positions.open(newline="", encoding="utf-8-sig") as handle:
        positions = list(csv.DictReader(handle))
    with args.sourcing_lock.open(newline="", encoding="utf-8-sig") as handle:
        locked = list(csv.DictReader(handle))

    placed_refs = [row["Ref"].strip() for row in positions]
    if len(placed_refs) != len(set(placed_refs)):
        raise SystemExit("duplicate references in position file")

    locked_refs: list[str] = []
    for row in locked:
        locked_refs.extend(refs(row["Designator"]))
    if len(locked_refs) != len(set(locked_refs)):
        raise SystemExit("duplicate references in sourcing lock")
    missing = sorted(set(placed_refs) - set(locked_refs))
    unexpected = sorted(set(locked_refs) - set(placed_refs))
    if missing or unexpected:
        raise SystemExit(
            f"BOM/CPL reference mismatch: missing={missing}, unexpected={unexpected}"
        )

    args.bom_output.parent.mkdir(parents=True, exist_ok=True)
    with args.bom_output.open("w", newline="", encoding="utf-8-sig") as handle:
        writer = csv.DictWriter(handle, fieldnames=[
            "Comment", "Designator", "Footprint", "LCSC Part #",
        ])
        writer.writeheader()
        for row in locked:
            writer.writerow({key: row[key] for key in writer.fieldnames})

    args.cpl_output.parent.mkdir(parents=True, exist_ok=True)
    with args.cpl_output.open("w", newline="", encoding="utf-8-sig") as handle:
        writer = csv.DictWriter(handle, fieldnames=[
            "Designator", "Mid X", "Mid Y", "Layer", "Rotation",
        ])
        writer.writeheader()
        for row in positions:
            writer.writerow({
                "Designator": row["Ref"].strip(),
                "Mid X": row["PosX"].strip(),
                "Mid Y": row["PosY"].strip(),
                "Layer": "Top" if row["Side"].strip().lower() == "top" else "Bottom",
                "Rotation": row["Rot"].strip(),
            })

    print(f"wrote {len(locked)} BOM groups and {len(positions)} placements")


if __name__ == "__main__":
    main()
