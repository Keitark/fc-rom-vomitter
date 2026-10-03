# NES-SDR integration branch

This branch adds an experimental, no-hardware-modification NES-SDR path to
FC ROM Vomitter.

## Current integration status

Implemented:

- partial CHR SRAM write + readback verification;
- controller-locked CHR refresh API;
- NES-SDR renderer/SPC1/live component compiled inside the ESP-IDF project;
- manual synthetic spectrum endpoint:
  `POST /api/nes-sdr/demo-frame`;
- browser button for one synthetic graph refresh.

Not yet integrated:

- ESP-SDR RF capture/FFT source;
- automatic ~1 Hz refresh task.

## First hardware test

1. Build/flash this branch.
2. Upload the NES-SDR NROM from `Keitark/nes-sdr`.
3. Press the Famicom RESET button once so the ROM initializes the PPU and parks
   the 2A03 at `$0200` internal RAM.
4. Open the ROM Vomitter web UI.
5. Press **NES-SDR demo frame**.
6. The spectrum bars should change without another Famicom RESET.

Each click advances the synthetic two-peak pattern by one column.

## ROM signature gate

The NES-SDR NROM embeds the fixed eight-byte signature `NES-SDR1` at CPU
address `$FFF0` (normalized PRG offset `0x7FF0`).

The demo endpoint verifies that signature before touching CHR SRAM. A normal
game or an older unsigned NES-SDR build is rejected with HTTP 409.

This is still an experimental live-write path, but accidental use on unrelated
ROMs is now blocked.

## Expected behavior

During each refresh the existing LOAD/RUN logic temporarily removes CHR from
the PPU. A brief visual disturbance is expected.

After RUN is restored:

- the nametable should remain intact;
- the static NES-SDR font and red axis should remain intact;
- only graph tiles 0-191 should change;
- the CPU should remain parked in internal RAM;
- no RESET should be required.

## Scope / logic analyzer

For first hardware validation capture:

- `LOAD_MODE`
- `CHR_WE_n`
- `CHR_OE_n`
- one PPU-side CHR data bit

The released board's core invariant remains mandatory: console-side and
MCU-side SRAM bus drivers must never be enabled simultaneously.

## Licensing boundary

This branch contains an integration copy of the NES-SDR component with its
source/provenance documented under `firmware/components/nes_sdr/README.md`.

ESP-SDR source has **not** been copied into this branch. ESP-SDR is
GPL-3.0-or-later; the eventual combined RF build must preserve that license
boundary explicitly.
