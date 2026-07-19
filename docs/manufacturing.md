# Manufacturing release notes

## Rev A-FC configuration

- 6 copper layers;
- 1.2 mm finished thickness;
- 90.0 × 66.8 mm maximum envelope;
- green solder mask;
- 60 wear-capable gold fingers with 45-degree insertion-edge bevel;
- no paid controlled-impedance option;
- top-side PCBA;
- 0.50/0.30 mm standard through vias;
- custom frameless top stencil derived from F.Paste.

The 10 mm extension at the edge opposite the fingers is intentional and needs
a custom shell. The standard tongue, side notches, centre slot and J1 datum may
not be normalized by a fabricator.

## Release evidence

The included release records:

| Gate | Result |
|---|---:|
| ERC | 0 errors; 8 reviewed warnings |
| Raw unconnected items | 0 |
| Classified real DRC errors | 0 |
| Layout-check failures | 0 |
| Unresolved CPL registrations | 0 |
| Unwaived silk overlap / over-copper | 0 / 0 |

Two reported silk-edge findings are the intentional USB connector mouth at the
board boundary. Gold-finger mask openings produce design-specific raw DRC items
that are classified separately; they are not silently deleted.

## Files

- `gerbers-drill.zip`: uploadable fabrication package.
- `assembly/02_nescart-fc-BOM-jlcpcb.csv`: JLCPCB-format BOM.
- `assembly/03_nescart-fc-CPL-jlcpcb-calibrated.csv`: calibrated placement file.
- `assembly/04_cpl-registration-audit.csv`: per-reference registration evidence.
- `evidence/`: board statistics, ERC/DRC/layout reports, renders and manufacturing views.
- `release-manifest.json`: SHA-256 hashes for source and every public release artifact.

Vendor placement preview is still a separate gate. Import-time visual checks
must cover every fitted reference, pin 1, polarity, body centering, rotation,
mirroring and X/Y translation. A browser-only correction is diagnostic; fix the
source mapping, regenerate, re-hash and re-upload.

The public package intentionally omits account details, order numbers, payment,
coupons, addresses and browser screenshots.
