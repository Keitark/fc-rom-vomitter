# Hardware source

`nescart-fc.kicad_pro` is the KiCad 10 entry point. The checked-in PCB and
schematic match the SHA-256 values recorded in
`../manufacturing/rev-a-fc/release-manifest.json`.

## Sources of truth

- Electrical connectivity: `tools/gen_sch.py` and its generated hierarchical sheets.
- FC60 edge geometry: `tools/fc_geometry.py` and `tools/gen_edge_footprint.py`.
- Released routing/placement: `nescart-fc.kicad_pcb` (revision-locked snapshot).
- Fabrication constraints: `docs/DESIGN-CONSTRAINTS.md`.
- Part and placement mapping: `docs/sourcing-lock.csv` and `docs/cpl-overrides.csv`.

Do not hand-edit generated schematic connectivity. Regenerate it from the
tables in `gen_sch.py`, then compare the resulting pin/net map and run ERC.

## Required release gates

1. ERC has zero errors; expected warnings must be reviewed.
2. Raw unconnected items are zero.
3. Classified DRC has zero real errors and zero power disconnects.
4. `layout_check.py` reports `0 FAIL`.
5. Both sides are rendered and visually inspected.
6. Every fitted CPL reference is reconciled and visually checked after vendor import.
7. Gerber, drill, BOM, CPL and evidence files come from the same PCB revision.

KiCad's bundled Python is required for scripts importing `pcbnew`.
