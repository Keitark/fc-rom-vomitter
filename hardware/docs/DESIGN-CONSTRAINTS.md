# nescart-fc Rev A-FC manufacturing constraints

These constraints accompany every fabrication and assembly quote. Browser or
vendor-side edits may not override them without a new reviewed release.

## PCB

- Quantity: 5 PCBs; assemble 3.
- Copper layers: 6.
- Finished thickness: 1.2 mm.
- Outline: irregular 90.0 mm x 66.8 mm maximum envelope with the released
  notches, centre slot, 78.4 mm tongue, and 10 mm custom-shell extension.
- Solder mask: green.
- Gold fingers: yes, wear-capable finish on the 60-pin Famicom edge.
- Edge bevel: 45 degrees on the insertion edge.
- Preserve the exact Edge.Cuts and J1 datum. No vendor outline normalization,
  rounding, panel tab, tooling-hole, or rail change may enter the finished PCB.
- Standard routed vias: 0.50/0.30 mm through vias. Flag every drill below
  0.30 mm and every quote option that triggers a premium process.
- No paid controlled-impedance option is required for this cost-controlled
  revision. Preserve the released USB differential routing and continuous GND
  reference; do not change trace geometry in the order interface.
- The 10 mm top extension is owner-approved for a custom 3D-printed shell.
  Compatibility with an original Famicom shell is not claimed.

## Assembly

- Assembly side: top only.
- DNP: J1 edge connector, J3 debug header, TP1 test pad, and LOGO1 artwork.
- Use the exact released BOM and calibrated CPL hashes.
- Design-critical parts requiring explicit substitution approval:
  `ESP32-S3-WROOM-1-N8`, `CY62128EV30LL-45SXIT`, and `SN74LVC245APWR`.
- CPL import must pass complete reference reconciliation and visual pin-1,
  polarity, terminal, tab, connector, and body-alignment review before user
  placement approval.

## Physical stencil

- Order together with the PCB as a custom-size frameless, top-only stencil.
- Outer size: 130 x 110 mm for the 90.0 x 66.8 mm PCB envelope. This leaves
  roughly 20 mm working margin around the design; do not cut the stencil to the
  exact PCB outline.
- Thickness: 0.12 mm.
- Source: released F.Paste Gerber only.
- Fiducials: none unless a later release adds matching fiducials.
- Remark: `Follow the top solder-paste layer only. Keep the PCB artwork centered in the 130 x 110 mm custom frameless stencil.`
