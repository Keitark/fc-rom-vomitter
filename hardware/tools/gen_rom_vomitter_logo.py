"""Convert the generated ROM Vomitter mascot into a KiCad silk footprint.

The raster is reduced to a coarse binary grid whose one-pixel features remain
above normal PCB silkscreen minimums.  Horizontal black runs are vertically
merged into filled rectangles, so holes and the X eyes survive without using
an embedded bitmap.
"""

from __future__ import annotations

import argparse
from pathlib import Path

import cv2
import numpy as np


HERE = Path(__file__).resolve().parent
FC = HERE.parent
DEFAULT_INPUT = FC / "assets" / "rom-vomitter-mascot.png"
DEFAULT_OUTPUT = FC / "nescart-fc.pretty" / "rom_vomitter_logo.kicad_mod"


def merged_rectangles(mask: np.ndarray) -> list[tuple[int, int, int, int]]:
    """Return inclusive-exclusive x/y rectangles for black mask runs."""
    active: dict[tuple[int, int], tuple[int, int, int, int]] = {}
    done: list[tuple[int, int, int, int]] = []
    for y, row in enumerate(mask):
        runs: list[tuple[int, int]] = []
        x = 0
        while x < row.size:
            if not row[x]:
                x += 1
                continue
            start = x
            while x < row.size and row[x]:
                x += 1
            runs.append((start, x))

        current = set(runs)
        for run in list(active):
            if run not in current:
                done.append(active.pop(run))
        for run in runs:
            if run in active:
                x0, y0, x1, _ = active[run]
                active[run] = (x0, y0, x1, y + 1)
            else:
                active[run] = (run[0], y, run[1], y + 1)
    done.extend(active.values())
    return done


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("--input", type=Path, default=DEFAULT_INPUT)
    ap.add_argument("--output", type=Path, default=DEFAULT_OUTPUT)
    ap.add_argument("--width-mm", type=float, default=22.0)
    ap.add_argument("--grid-width", type=int, default=96)
    ap.add_argument("--threshold", type=int, default=185)
    args = ap.parse_args()

    gray = cv2.imread(str(args.input), cv2.IMREAD_GRAYSCALE)
    if gray is None:
        raise SystemExit(f"cannot read {args.input}")
    ink = gray < args.threshold
    ys, xs = np.where(ink)
    if not len(xs):
        raise SystemExit("generated image contains no dark artwork")
    pad = 8
    x0 = max(0, int(xs.min()) - pad)
    x1 = min(gray.shape[1], int(xs.max()) + pad + 1)
    y0 = max(0, int(ys.min()) - pad)
    y1 = min(gray.shape[0], int(ys.max()) + pad + 1)
    crop = gray[y0:y1, x0:x1]

    grid_h = max(1, round(args.grid_width * crop.shape[0] / crop.shape[1]))
    small = cv2.resize(crop, (args.grid_width, grid_h), interpolation=cv2.INTER_AREA)
    mask = small < args.threshold
    mask = cv2.morphologyEx(mask.astype(np.uint8), cv2.MORPH_CLOSE,
                            np.ones((2, 2), np.uint8)).astype(bool)

    # Remove isolated one-pixel noise while retaining data blocks and IC pins.
    n, labels, stats, _ = cv2.connectedComponentsWithStats(mask.astype(np.uint8), 8)
    clean = np.zeros_like(mask)
    for label in range(1, n):
        if stats[label, cv2.CC_STAT_AREA] >= 2:
            clean[labels == label] = True

    rects = merged_rectangles(clean)
    pixel = args.width_mm / args.grid_width
    height = clean.shape[0] * pixel
    left = -args.width_mm / 2
    top = -height / 2

    lines = [
        '(footprint "rom_vomitter_logo"',
        '  (version 20240108)',
        '  (generator "gen_rom_vomitter_logo.py")',
        '  (layer "F.Cu")',
        '  (attr board_only exclude_from_pos_files exclude_from_bom)',
        '  (fp_text reference "LOGO1" (at 0 0) (layer "F.SilkS") hide',
        '    (effects (font (size 1 1) (thickness 0.15))))',
        '  (fp_text value "ROM_VOMITTER_MASCOT" (at 0 0) (layer "F.Fab") hide',
        '    (effects (font (size 1 1) (thickness 0.15))))',
    ]
    for rx0, ry0, rx1, ry1 in rects:
        ax = left + rx0 * pixel
        ay = top + ry0 * pixel
        bx = left + rx1 * pixel
        by = top + ry1 * pixel
        lines.append(
            f'  (fp_rect (start {ax:.4f} {ay:.4f}) (end {bx:.4f} {by:.4f}) '
            '(stroke (width 0) (type solid)) (fill solid) (layer "F.SilkS"))'
        )
    lines.append(')')
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text("\n".join(lines) + "\n", encoding="utf-8")
    print(f"wrote {args.output} grid={clean.shape[1]}x{clean.shape[0]} "
          f"pixel={pixel:.4f}mm rectangles={len(rects)}")


if __name__ == "__main__":
    main()
