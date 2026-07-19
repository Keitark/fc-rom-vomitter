"""Add the ROM Vomitter identity block to a named FC PCB candidate.

Only B.SilkS graphics are added.  The source board is never overwritten and a
basename-matched project file is copied beside the candidate.
"""

from __future__ import annotations

import argparse
import json
import shutil
from pathlib import Path

import pcbnew


HERE = Path(__file__).resolve().parent
FC = HERE.parent
DEFAULT_BOARD = FC / "nescart-fc.kicad_pcb"
DEFAULT_OUTPUT = FC / "states" / "232-rom-vomitter-silk.kicad_pcb"
PRETTY = FC / "nescart-fc.pretty"
IDENTITY_TEXT = {
    "ROM VOMITTER",
    "DESIGNED WITH FABLE 5 + GPT-5.6",
    "2026-07-15",
}


def mm(x: float, y: float) -> pcbnew.VECTOR2I:
    return pcbnew.VECTOR2I(pcbnew.FromMM(x), pcbnew.FromMM(y))


def add_text(board: pcbnew.BOARD, value: str, x: float, y: float,
             height: float, thickness: float) -> str:
    text = pcbnew.PCB_TEXT(board)
    text.SetText(value)
    text.SetPosition(mm(x, y))
    text.SetLayer(pcbnew.B_SilkS)
    text.SetMirrored(True)
    text.SetTextSize(mm(height, height))
    text.SetTextThickness(pcbnew.FromMM(thickness))
    text.SetHorizJustify(pcbnew.GR_TEXT_H_ALIGN_CENTER)
    text.SetVertJustify(pcbnew.GR_TEXT_V_ALIGN_CENTER)
    board.Add(text)
    return str(text.m_Uuid)


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("--board", type=Path, default=DEFAULT_BOARD)
    ap.add_argument("--output", type=Path, default=DEFAULT_OUTPUT)
    ap.add_argument("--report", type=Path)
    ap.add_argument("--logo-x", type=float, default=48.0)
    ap.add_argument("--logo-y", type=float, default=34.0)
    ap.add_argument("--text-x", type=float, default=83.0)
    args = ap.parse_args()
    board_path = args.board.resolve()
    output = args.output.resolve()
    if board_path == output:
        raise SystemExit("refusing to overwrite the source board")
    source_pro = board_path.with_suffix(".kicad_pro")
    if not source_pro.exists():
        raise SystemExit(f"paired project file missing: {source_pro}")

    board = pcbnew.LoadBoard(str(board_path))
    removed = []
    for fp in list(board.GetFootprints()):
        if fp.GetReference() == "LOGO1":
            removed.append(str(fp.m_Uuid))
            board.Remove(fp)
    for item in list(board.GetDrawings()):
        if isinstance(item, pcbnew.PCB_TEXT) and item.GetText() in IDENTITY_TEXT:
            removed.append(str(item.m_Uuid))
            board.Remove(item)

    logo = pcbnew.FootprintLoad(str(PRETTY), "rom_vomitter_logo")
    if logo is None:
        raise SystemExit("failed to load rom_vomitter_logo footprint")
    logo.SetReference("LOGO1")
    # KiCad 10's SWIG wrapper requires the footprint to belong to a board
    # before Flip() is called; flipping an unattached library footprint can
    # access-violate instead of raising a Python exception.
    board.Add(logo)
    logo.SetPosition(mm(args.logo_x, args.logo_y))
    logo.Flip(logo.GetPosition(), False)

    added = {
        "logo_uuid": str(logo.m_Uuid),
        "texts": [
            add_text(board, "ROM VOMITTER", args.text_x, 29.8, 2.6, 0.42),
            add_text(board, "DESIGNED WITH FABLE 5 + GPT-5.6", args.text_x,
                     34.0, 1.0, 0.18),
            add_text(board, "2026-07-15", args.text_x, 37.2, 1.2, 0.20),
        ],
    }

    output.parent.mkdir(parents=True, exist_ok=True)
    pcbnew.SaveBoard(str(output), board)
    output_pro = output.with_suffix(".kicad_pro")
    shutil.copy2(source_pro, output_pro)

    report = args.report or output.with_suffix(".silk.json")
    report.write_text(json.dumps({
        "schema": "rom-vomitter-silk-v1",
        "source": str(board_path),
        "output": str(output),
        "side": "B.SilkS",
        "logo_position_mm": [args.logo_x, args.logo_y],
        "text_center_x_mm": args.text_x,
        "removed_previous_identity": removed,
        "added": added,
        "inscription": [
            "ROM VOMITTER",
            "DESIGNED WITH FABLE 5 + GPT-5.6",
            "2026-07-15",
        ],
    }, indent=2) + "\n", encoding="utf-8")
    print(f"wrote {output} and {output_pro}")


if __name__ == "__main__":
    main()
