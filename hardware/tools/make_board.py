"""Create a clean nescart-fc board with a measured Famicom outline and J1.

The outer geometry is reconstructed from the HVC-TGROM-01 dimensions
published by NESdev/Gumball2415. The standard top-left corner was
(24.85, 20.00) mm; the user-approved routing-area extension moves only that
top edge to y=10.00 mm.

Run with KiCad 10's bundled Python.  The production board is normally derived
from the frozen NES layout by derive_from_nes_layout.py; this clean generator
exists as an independent mechanical/source-of-truth check.
"""

import os
import pcbnew

from fc_geometry import EDGE_AT, OUTLINE, TOP_EXTENSION, TOP_Y


HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.normpath(os.path.join(HERE, "..", "nescart-fc.kicad_pcb"))
PRETTY = os.path.normpath(os.path.join(HERE, "..", "nescart-fc.pretty"))

def mm(value: float) -> int:
    return pcbnew.FromMM(value)


def main() -> None:
    board = pcbnew.CreateEmptyBoard()
    for x1, y1, x2, y2 in OUTLINE:
        segment = pcbnew.PCB_SHAPE(board)
        segment.SetShape(pcbnew.SHAPE_T_SEGMENT)
        segment.SetStart(pcbnew.VECTOR2I(mm(x1), mm(y1)))
        segment.SetEnd(pcbnew.VECTOR2I(mm(x2), mm(y2)))
        segment.SetLayer(pcbnew.Edge_Cuts)
        segment.SetWidth(mm(0.1))
        board.Add(segment)

    footprint = pcbnew.FootprintLoad(PRETTY, "cartridge_edge_60")
    if footprint is None:
        raise SystemExit("edge footprint not found in " + PRETTY)
    footprint.SetReference("J1")
    footprint.SetPosition(pcbnew.VECTOR2I(mm(EDGE_AT[0]), mm(EDGE_AT[1])))
    footprint.SetLocked(True)
    board.Add(footprint)

    board.GetDesignSettings().SetBoardThickness(mm(1.2))
    pcbnew.SaveBoard(OUT, board)
    print("wrote", OUT)


if __name__ == "__main__":
    main()
