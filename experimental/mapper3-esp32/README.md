# ESP32-S3 two-bank Mapper-3 experiment

[日本語](README_JA.md) · [Hardware patch](HARDWARE-PATCH.md) · [Validation record](validation.json)

This is a post-release experiment for the existing FC ROM Vomitter Rev A-FC
board. It observes Mapper-3 CPU writes with the onboard ESP32-S3 and drives
CHR SRAM A13, allowing two 8 KiB CHR banks without adding another MCU or a
dedicated latch.

> [!CAUTION]
> This is a bodge-wire experiment, not a revised fabrication release. Do not
> order or modify hardware from this directory without reviewing the rework,
> measuring the input levels, and accepting the risk of lifted pins.

The immutable NROM release remains in the repository root. The checked-in
`hardware/` schematic and PCB match the Rev A-FC manufacturing source; this
experiment does not edit either one.

## Compatibility boundary

| Mode | Supported by this experiment |
|---|---|
| Mapper 0 / NROM | 16 or 32 KiB PRG, one 8 KiB CHR bank |
| Mapper 3 / CNROM | 16 or 32 KiB PRG, exactly two 8 KiB CHR banks, bank selected by CPU D0 |
| CHR-RAM, four-bank CNROM, UNROM, MMC1/MMC3, expansion audio | Not supported |

Mapper-0 CHR is copied to both physical CHR regions. Mapper 3 uses GPIO36 to
select U14 A13 at runtime.

## Signal paths

| Function | ESP32-S3 | Board signal / point |
|---|---:|---|
| CHR bank output | GPIO36 | TP1 to isolated U14 pin 28 (A13) |
| Mapper data bit | GPIO42 | CPU D0 through the preferred U2 spare channel |
| Cartridge selection | GPIO43 | U19 pin 4, `ROMSEL_inv`, via J3 pin 2 |
| Read rejection | GPIO44 | U21 pin 4, `PRG_EN_n`, via J3 pin 3 |

See [HARDWARE-PATCH.md](HARDWARE-PATCH.md) before soldering. It separates the
preferred buffered path from the resistor-divider repair used after damage to
one physical U2 input lead. The divider is recorded as evidence, not presented
as the production recommendation.

## Runtime design

- ESP32-S3 core 1 runs a 240 MHz interrupt-disabled polling loop from IRAM.
- CPU D0, `ROMSEL_inv`, and `PRG_EN_n` are read together from one GPIO input
  register.
- A selected write is confirmed by a second immediate snapshot before D0 is
  written to GPIO36.
- Mapper polling starts before LOAD-to-RUN handoff and is gated during SRAM
  loading.
- Wi-Fi, lwIP, and HTTP tasks remain on core 0.
- `/api/status` reports the selected bank and accepted bank-write counters.
- `/api/mapper-trace` returns saved raw bus windows. Pressing BOOT stops the
  mapper loop and saves the trace to NVS; reset is required to resume play.

## Build and test

```powershell
cmake -S host_tests -B host_tests/build
cmake --build host_tests/build --config Release
ctest --test-dir host_tests/build -C Release --output-on-failure

pio run -e esp32-s3 -j 1
```

The single-job PlatformIO form is intentional on Windows systems where a
parallel ESP-IDF build may exhaust local resources.

The host tests cover iNES normalization, the atomic ROM slots, mapper truth
tables, short selected writes, delayed D0, read-start hazards, repeated bank
writes, and interruption by LOAD mode.

## Evidence status

As of 2026-08-07:

- host tests: **PASS**;
- ESP32-S3 / ESP-IDF 5.5 build: **PASS**;
- hot loop in `.iram0.text`, direct GPIO access, no function calls: **PASS**;
- one modified Rev A-FC board: Mapper 0 regression passed;
- one two-bank Mapper-3 homebrew target: final behavior was near the emulator
  reference after the shortened polling path;
- powered input margin, worst-case console timing, long-run Wi-Fi stress, and
  multiple-board reproducibility: **USER_REVIEW / not qualified**.

This supports continued experimentation. It does not justify relabeling the
main Rev A-FC release as Mapper-3 capable.
