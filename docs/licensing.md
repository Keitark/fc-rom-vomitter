# Licensing and attribution

This repository is intentionally dual-licensed.

## MIT

The root [MIT license](../LICENSE) applies to:

- `firmware/`;
- `scripts/`;
- Python/PowerShell source under `hardware/tools/`, except where a file states otherwise;
- README files and original general documentation under `docs/`, except designated hardware-derived images and manufacturing views.

## CC BY-SA 4.0

[CC BY-SA 4.0](../LICENSES/CC-BY-SA-4.0.txt) applies to:

- KiCad design and library files under `hardware/`;
- hardware-specific documentation under `hardware/docs/`;
- `manufacturing/` artifacts;
- experimental KiCad hardware and hardware-derived evidence under `experimental/nes/`;
- PCB renders and hardware artwork under `docs/images/` and `hardware/assets/`.

Attribution: **Keitark, FC ROM Vomitter / nescart-fc Rev A-FC**,
<https://github.com/Keitark/fc-rom-vomitter>.

## Third-party material and references

- Famicom connector behavior and pinout research: [NESdev Wiki cartridge connector](https://www.nesdev.org/wiki/Cartridge_connector).
- Mechanical dimension research: [NESdev Famicom cartridge dimensions](https://www.nesdev.org/wiki/Famicom_cartridge_dimensions) and [Gumball2415/NES-Famicom-Cartridge-Dimensions](https://github.com/Gumball2415/NES-Famicom-Cartridge-Dimensions). The FC outline was reconstructed from published dimensions rather than copied from an upstream KiCad file.
- `hardware/nescart-fc.3dshapes/SSOP-32_11.305x20.495mm_P1.27mm.step`: KiCad Packages3D 8.0.5, copyright 2018 KiCad StepUp, CC BY-SA 4.0 with the KiCad library exception. See the adjacent README and STEP header.
- The experimental NES cartridge edge footprint and board geometry derive from [emeargt/nes-cnrom](https://github.com/emeargt/nes-cnrom), licensed CC BY-SA 4.0.
- The ROM Vomitter mascot is original generated artwork; its source prompt is preserved next to the asset.

Facts such as connector pin assignments are cited for traceability. References
do not imply endorsement by NESdev, KiCad, Nintendo or other contributors.

Famicom and Nintendo are trademarks of Nintendo. This project is not affiliated
with or endorsed by Nintendo.
