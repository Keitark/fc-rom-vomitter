# Famicom 60-pin interface and mechanical baseline

This is the implementation record for `nescart-fc` Rev A-FC.  The frozen
NES-001 design under `hardware/` remains unchanged.

## Mechanical baseline

- [SPEC] Standard Japanese Famicom cartridge PCB body: 90.0 mm wide.
- [TARGET] Rev A-FC keeps that 90.0 mm width and extends only the edge
  opposite the gold fingers by 10.0 mm to create routing and component area.
  The connector tongue, side notches, centre slot, and J1 datum do not move.
- [SPEC] Connector tongue: 78.4 mm wide, 10.7 mm long.
- [SPEC] PCB thickness: 1.2 mm.
- [SPEC] Contact pitch: 2.54 mm.
- [SPEC] The body side notches and the 5 x 2 mm centre boss slot are board
  voids: pads, copper, vias, and component bodies may not cross them.
- [SPEC] Board-label side is KiCad B.Cu; component side is F.Cu.
- [SPEC] The owner approved the 10 mm top extension for a custom 3D-printed
  shell on 2026-07-15. Compatibility with an original Famicom cartridge shell
  is intentionally not claimed. The standard-width tongue, J1 datum, notches,
  centre slot, and 1.2 mm mating thickness remain fixed for the console.

The tongue, width, side notches, centre slot, and connector datum were
reconstructed from the published HVC-TGROM dimensional reference rather than
copied from an upstream KiCad file. The 10 mm top extension is a project-specific
user-approved custom-shell target and is not present in that reference. Sources:

- <https://www.nesdev.org/wiki/Famicom_cartridge_dimensions>
- <https://github.com/Gumball2415/NES-Famicom-Cartridge-Dimensions>

## Electrical pin map

| Pin | nescart-fc net | Function |
|---:|---|---|
| 1 | GND | Ground |
| 2-13 | `CPU_A11` ... `CPU_A0` | CPU address |
| 14 | `CPU_RW` | CPU read/write |
| 15 | NC | `/IRQ`, unused in Rev A-FC |
| 16 | GND | Ground |
| 17 | `PPU_RD_n` | PPU read strobe |
| 18 | `CIRAM_A10` | Nametable mirroring select |
| 19-25 | `PPU_A6` ... `PPU_A0` | PPU address |
| 26-29 | `PPU_D0` ... `PPU_D3` | PPU data |
| 30 | `NES_5V` | Console +5 V, before the OR-ing diode |
| 31 | `NES_5V` | Cartridge-present/+5 V bridge to pin 30 |
| 32 | NC | M2, unused in Rev A-FC |
| 33-35 | `CPU_A12` ... `CPU_A14` | CPU address |
| 36-43 | `CPU_D7` ... `CPU_D0` | CPU data |
| 44 | `ROMSEL_n` | PRG ROM select |
| 45 | `FC_AUDIO_FROM_CONSOLE` | Audio from console/2A03 |
| 46 | `FC_AUDIO_TO_CONSOLE` | Audio returned toward RF/AV path |
| 47 | NC | PPU write, unused in NROM Rev A-FC |
| 48 | `PPU_A13_n` | CIRAM `/CE` |
| 49 | `PPU_A13_n` | Inverted PPU A13 |
| 50-55 | `PPU_A7` ... `PPU_A12` | PPU address |
| 56 | NC | Non-inverted PPU A13, unused |
| 57-60 | `PPU_D7` ... `PPU_D4` | PPU data |

Authoritative connector reference:
<https://www.nesdev.org/wiki/Cartridge_connector>.

## Design-specific decisions

- [SPEC] Pins 30 and 31 are tied together on `NES_5V`.  The tie is before D2,
  so USB power cannot backfeed the console through this bridge.
- [SPEC] R20, 0 ohm, is the only connection from pin 45 to pin 46.  Fit R20
  unless a future audio mixer replaces it.
- [SPEC] No CIC circuit is fitted on the native Famicom variant.
- [SPEC] U23 GPIO36 is a spare signal exposed only at TP1.
- [SPEC] R9 pulls `MIRROR_SEL` low at startup.
- [SPEC] SW1 is ESP reset (`ESP RST`), not console reset.

## Regeneration checks

Before accepting a regenerated board, assert all of the following:

1. J1 has exactly 60 physical pads.
2. Pins 1/16 are GND; 30/31 are `NES_5V`.
3. Pins 45/46 terminate only through R20.
4. Pins 48/49 share `PPU_A13_n`; pin 56 is unconnected.
5. Pin 60 is `PPU_D4`, never GND.
6. All four outline voids remain free of physical pads and copper.
