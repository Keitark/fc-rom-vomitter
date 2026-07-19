# Rev A-FC layout and electrical guidance

The annotations below are design inputs for layout review, not a substitute
for measured hardware validation.

## Layers and return paths

- [TARGET] First closure uses six copper layers.
- [TARGET] In1.Cu is an uninterrupted GND reference plane.
- [TARGET] In4.Cu is a +3V3 plane; do not route signals on either dedicated
  plane layer.
- [TARGET] Route signals on F.Cu, In2.Cu, In3.Cu, and B.Cu.
- [TARGET] A later four-layer cost experiment may use F.Cu / In1 GND /
  In2 signal / B.Cu.  Do not consume both inner layers as dedicated planes.
- [SPEC] No copper, vias, or traces in the ESP32 antenna keepout, Famicom
  tongue keepout, side notches, or centre boss slot.

The six-layer baseline is driven by measured routing density: the frozen NES
route used all four signal layers, while the FC board retains roughly the same
net count and single-sided functional corridors. The 10 mm top extension adds
placement/routing area but does not itself prove a four-layer route. The layer
choice is not justified merely by the nominal bus clock.

## Geometry and fabrication cost

- [TARGET] Keep the Famicom insertion geometry fixed: 90.0 mm body width,
  78.4 mm tongue width, existing notches/slot, and unchanged J1 datum.
- [TARGET] Use the user-approved 10.0 mm extension only at the board edge
  opposite the gold fingers; do not widen or move the connector tongue.
- [SPEC] The owner approved a custom 3D-printed shell for the taller outline
  on 2026-07-15. Preserve the standard tongue, notches, centre slot, J1 datum,
  and 1.2 mm console-mating thickness; do not claim original-shell fit.
- [TARGET] Standard through via: 0.5/0.3 mm or 0.6/0.3 mm diameter/drill.
- [SPEC] Do not use 0.3/0.15 mm fine vias without a separately approved cost
  and manufacturability experiment.
- [TARGET] 0.20 mm general signal width; increase power paths from calculated
  current and temperature-rise requirements.
- [SPEC] Board thickness is 1.2 mm for cartridge fit.
- [SPEC] The edge contacts require a wear-capable hard-gold/gold-finger
  process; ordinary ENIG on the rest of the board is not a substitute.

## Buses and control signals

- [TARGET] CPU/PRG group operates near 1.79 MHz; group length spread <=25 mm.
- [TARGET] PPU/CHR group operates near 5.37 MHz; group length spread <=15 mm.
- [TARGET] ESP32 load bus firmware target <=10 MHz; group spread <=25 mm.
- [TARGET] USB Full-Speed D+/D- differential impedance 90 ohm +/-10%, pair
  skew <=1 mm, no stubs, and minimize vias.
- [TARGET] Preserve edge -> console buffer -> SRAM and ESP -> MCU buffer ->
  SRAM placement order.  Keep `/OE`, `/WE`, and `/CE` direct.
- [TARGET] Prefer short, monotonic routes; do not add trombone tuning merely
  to make low-frequency buses numerically equal.

Fast 74LVC edge rates, not only clock frequency, make continuous return paths
and short stubs important.

## Power and thermal signoff

- [TARGET] Size +5V and +3V3 source trunks for at least 0.8 A peak.
- [TARGET] Reserve at least 0.5 A transient capability for the ESP32 branch.
- [TARGET] Budget <=60 mA for each SRAM and 0.1 A for logic/LED overhead until
  measured values replace these provisional allocations.
- [TBD-MEASURE] Measure console supply capability, all rail currents, ESP32
  upload transients, and regulator temperature before fabrication signoff.
- [TBD-MEASURE] AMS1117 dissipation at the worst accepted input/current point
  requires thermal proof; 0.8 A with a 1.7 V drop would be about 1.36 W.
- [TARGET] Each IC has a local 100 nF capacitor in the VCC escape path and a
  short independent GND-via return.  Place bulk capacitance at each source
  entry and at the ESP32 branch.

## Release gates

Release requires all of the following, independently:

1. Zero unexplained raw opens and zero real power disconnects.
2. Zero real DRC errors after classified review; zero-open alone is invalid.
3. Visual inspection of routing, planes, notches, slot, tongue, USB contact
   row, ESP antenna region, silkscreen, and 3D mechanical fit.
4. CPL registration and orientation inspection for every fitted component.
5. Separate user approvals for substitutions, placement, final price, and
   payment.
