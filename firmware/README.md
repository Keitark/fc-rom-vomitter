# ROM Vomitter FC firmware

ESP-IDF firmware for the released `hardware-fc` ESP32-S3-WROOM-1-N8 board.
The new exhibition queue client builds, but remains **hardware-unvalidated**:
building it does not mean it has been flashed to, or tested on, the cartridge.

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
- Optional exhibition mode: signed polling of the Cloudflare queue. A visitor
  upload waits until an operator presses **Send next game**; the cartridge then
  fetches one released ROM, independently verifies its bytes and hashes, and
  uses the same flash/SRAM installation path. The original fixed-URL pull mode
  remains selectable.

The default SoftAP password is `vomit-roms`; change it in menuconfig for any
public or shared deployment.

## Exhibition queue configuration (not yet tested on hardware)

In `menuconfig`, enable cloud pull and its **operator-dispatched queue** mode.
Set the venue Wi-Fi SSID/password, the service origin (for example,
`https://fc-rom-vomitter-expo-staging.keitark.workers.dev`), a unique device ID,
and a 32–256 character device HMAC secret matching the Worker's private secret.
Enable **allow console reload** only for a supervised bench test after proving
bus isolation and reset behavior with an oscilloscope. When enabled, an
operator dispatch can interrupt a powered-on game; after READY, press the
Famicom's RESET button. Otherwise the cartridge waits for a safe power state.

The device credentials are compile-time settings. Never commit a generated
`sdkconfig` containing them or distribute a credential-bearing binary. The
`esp32-s3-cloud-ci` build uses dummy credentials solely to check compilation.
The SoftAP upload remains the local recovery path; the public Worker cannot
directly push bytes to a cartridge that is offline or not running this mode.

The queue client here is built from the mapper-0 firmware. The separately
patched Mapper 3/CNROM cartridge runs a timing-critical bank-switch task and
**must not be flashed with this binary**: its mapper support is not integrated
into this target. Before an exhibition deployment, merge the queue client into
that firmware, keep HTTP on core 0 and the bank-switch loop on core 1, then
measure bank-switch correctness and 3.3 V rail behavior during idle polling,
download, and powered-console reload. Wi-Fi/polling must not be called
``no impact'' merely because this target compiles.

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
