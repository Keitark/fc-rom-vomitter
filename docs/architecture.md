# Architecture and safety invariants

## Operating model

The Famicom starts much faster than the ESP32-S3. Rev A-FC therefore does not
promise an automatic clean boot. During loader startup, hardware defaults keep
the MCU and console from driving the SRAM buses simultaneously. Once a valid
image is loaded and verified, the user starts execution with the console RESET
button.

```mermaid
sequenceDiagram
    participant FC as Famicom
    participant Gate as Buffer ownership logic
    participant ESP as ESP32-S3
    participant Flash as Atomic flash slots
    participant SRAM as PRG/CHR SRAM

    FC->>Gate: Console power rises
    Gate->>FC: Console bus isolated from SRAM
    ESP->>Flash: Select last valid committed image
    ESP->>SRAM: Copy PRG + CHR in LOAD mode
    ESP->>SRAM: Byte-for-byte readback + CRC
    ESP->>Gate: Change ownership only while MCU/console are isolated
    Gate->>FC: RUN mode; SRAM visible to console
    ESP-->>FC: Solid blue READY indication
    Note over FC: User presses console RESET
    FC->>SRAM: Fetch reset vector and execute ROM
```

## Non-negotiable invariants

- Console-side buffers and MCU-side drivers must never be enabled together.
- Bus ownership changes only through the isolated transition state.
- All ESP32 bus GPIOs remain Hi-Z or at defined safe levels until firmware takes control.
- SRAM contents are disposable; the committed ESP32 flash slot is authoritative.
- SRAM readback must pass before RUN mode.
- USB-only bench power must not backfeed the console edge.
- The Famicom audio path must return through fitted R20 (0 Ω), or the console may be silent.
- The console-facing tongue, notches, centre slot, J1 datum and 1.2 mm thickness are mechanical constraints, not layout suggestions.

## Supported ROM model

Rev A-FC deliberately implements mapper 0 only:

- iNES input;
- 16 KiB or 32 KiB PRG ROM;
- 8 KiB CHR ROM;
- horizontal or vertical nametable mirroring;
- optional trainer stripping;
- NROM-128 PRG mirroring.

Other mappers, save RAM, expansion audio mixing and instant-on stub ROMs are future work.

## Power

The board can be powered from the console or USB-C for isolated bench work.
The current release uses an AMS1117-class 3.3 V regulator and must pass current,
dropout and thermal measurement before console use. A successful firmware build
or DRC result is not evidence of a safe power budget.
