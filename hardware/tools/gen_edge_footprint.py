"""Generate the original nescart-fc 60-pin Famicom card-edge footprint.

Geometry is reconstructed from the dimensional measurements published at:
https://www.nesdev.org/wiki/Famicom_cartridge_dimensions

The footprint is not copied from the upstream KiCad source.  B.Cu is the
label side and F.Cu is the component side, matching the usual Nintendo board
orientation.  Therefore pin 1 appears at the board-coordinate right edge.
"""

from pathlib import Path


OUT = Path(__file__).resolve().parent.parent / "nescart-fc.pretty" / "cartridge_edge_60.kicad_mod"


def pad(number: int, layer: str, x: float, y: float, width: float, height: float) -> str:
    return (
        f'  (pad "{number}" smd rect (at {x:.2f} {y:.2f}) '
        f'(size {width:.2f} {height:.2f}) (layers "{layer}"))'
    )


def main() -> None:
    lines = [
        '(footprint "cartridge_edge_60"',
        '  (version 20240108)',
        '  (generator "gen_edge_footprint")',
        '  (layer "F.Cu")',
        '  (attr board_only exclude_from_pos_files exclude_from_bom)',
        '  (fp_text reference "J1" (at 0 -2.5 0) (layer "F.SilkS")',
        '    (effects (font (size 1 1) (thickness 0.15))))',
        '  (fp_text value "FAMICOM 60-PIN / LABEL SIDE = B.Cu" (at 0 12.5 0) (layer "F.Fab")',
        '    (effects (font (size 1 1) (thickness 0.15))))',
        '  (fp_rect (start -39.2 0) (end 39.2 10.7)',
        '    (stroke (width 0.1) (type solid)) (fill solid) (layer "F.Mask"))',
        '  (fp_rect (start -39.2 0) (end 39.2 10.7)',
        '    (stroke (width 0.1) (type solid)) (fill solid) (layer "B.Mask"))',
        '  (fp_rect (start -39.2 0) (end 39.2 10.7)',
        '    (stroke (width 0.12) (type solid)) (fill none) (layer "Dwgs.User"))',
    ]

    # Measured pitch is 2.54 mm.  Pins 1/30/31 are wider power or
    # cartridge-present contacts.  Pin 16 is the longer GND contact.
    xs = {1: 37.33, 30: -37.33}
    for number in range(2, 30):
        xs[number] = 34.29 - (number - 2) * 2.54

    for number in range(1, 31):
        long_contact = number in {1, 16, 30}
        width = 2.60 if number in {1, 30} else 1.60
        height = 6.70 if long_contact else 5.70
        y = 6.35 if long_contact else 5.85
        lines.append(pad(number, "B.Cu", xs[number], y, width, height))

    for number in range(31, 61):
        source = number - 30
        # Pin 60 is a normal signal contact at the regular 2.54 mm pitch;
        # unlike pin 30 it has no half-millimetre power-contact offset.
        x = -36.83 if number == 60 else xs[source]
        long_contact = number == 31
        width = 2.60 if long_contact else 1.60
        height = 6.70 if long_contact else 5.70
        y = 6.35 if long_contact else 5.85
        lines.append(pad(number, "F.Cu", x, y, width, height))

    lines.append(')')
    OUT.parent.mkdir(parents=True, exist_ok=True)
    OUT.write_text("\n".join(lines) + "\n", encoding="utf-8")
    print(f"wrote {OUT}")


if __name__ == "__main__":
    main()
