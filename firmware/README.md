# ROM Vomitter FC firmware

ESP-IDF firmware prepared for the released `hardware-fc` ESP32-S3-WROOM-1-N8
board. It is intentionally marked **hardware-unvalidated** until the assembled
boards arrive.

## Current verification status

- **PASS:** host tests for iNES normalization, CRC32, and atomic slot selection.
- **PASS:** ESP32-S3-N8 target build with PlatformIO and ESP-IDF 5.5.0.
- **PENDING HARDWARE:** native-USB flashing, GPIO timing/polarity, SRAM walking-bit
  tests, current/thermal measurements, Wi-Fi operation, and Famicom boot.

A successful build proves that the firmware compiles and fits. It does not prove
the released PCB's electrical behavior, so the first-board gate below remains
mandatory.

## Implemented preparation

- Released FC GPIO map from `hardware-fc/tools/gen_sch.py`.
- Safe LOAD/RUN sequencing with MCU and console data buffers disabled before
  ownership changes.
- Strict mapper-0 iNES validation: 16/32 KiB PRG, 8 KiB CHR, horizontal or
  vertical mirroring, optional trainer stripping, NROM-128 PRG mirroring.
- Two 64 KiB flash slots. Payload is written and CRC-verified before the commit
  metadata is written, so power loss cannot erase the previous valid image.
- PRG and CHR SRAM write plus byte-for-byte readback verification.
- Wi-Fi SoftAP and browser upload page at `http://192.168.4.1`.
- FC-specific UX: solid blue means READY; then press the **console's red RESET
  button**. Slow blink means no image; fast blink means transfer/load.
- Conservative 11 dBm Wi-Fi TX cap until rail and regulator measurements exist.

The default SoftAP password is `vomit-roms`; change it in menuconfig for any
public or shared deployment.

## Build without a board

```powershell
cd firmware
pio run -e esp32-s3
```

Host-test the parser, CRC and atomic-slot selection:

```powershell
cmake -S host_tests -B host_tests/build -G "Visual Studio 16 2019" -A x64
cmake --build host_tests/build --config Release
ctest --test-dir host_tests/build -C Release --output-on-failure
```

## First-board bring-up gate

Do not insert an untested board into a Famicom. Use current-limited USB bench
power first:

```powershell
cd firmware
pio run -e esp32-s3 -t upload --upload-port COMx
pio device monitor --port COMx --baud 115200
```

Replace `COMx` with the native-USB port. If automatic download mode does not
start, hold **BOOT**, tap **ESP RST**, release **BOOT**, and retry the upload.

1. Inspect 5 V and 3.3 V rails, regulator temperature, and idle current.
2. Flash over native USB using BOOT + ESP RST; confirm UART/USB logs.
3. With no console attached, verify the blue LED patterns and SoftAP upload.
4. Probe `LOAD_MODE`, both MCU enable lines, `/WE`, and `/OE`; confirm no
   simultaneous MCU/console ownership.
5. Run walking-bit and address tests on both SRAMs before loading a ROM.
6. Confirm flash-slot power-loss recovery by interrupting an upload.
7. Only then insert with console power off, wait for solid READY, and press the
   console RESET button to run the test ROM.

The loader deliberately remains in isolated LOAD topology when only USB power
is present. A console-power rising event reloads/verifies SRAM before RUN.
