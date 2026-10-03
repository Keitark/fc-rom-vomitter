# NES-SDR live integration (bench candidate)

This is an opt-in ESP32-S3 firmware profile for Issue #1 in
`Keitark/nes-sdr`. The standard `esp32-s3` profile keeps the original SoftAP,
USB upload, and cloud behavior. Live mode uses the radio in receive-only NULL
mode, so the SoftAP and cloud client are unavailable in that profile. USB ROM
upload remains available; after arming, a PC is not in the spectrum data path.

## Source and build

Use sibling checkouts of `Keitark/nes-sdr` and `ESPARGOS/esp-sdr` at the
revisions documented by the issue. Apply the companion NES-SDR integration
patch to the ESP-SDR checkout before building. Initialize its pinned `esp-dsp`
submodule. The NES-SDR checkout must contain the `NES-SDR-LIVE-V1` ROM marker.
No ESP-SDR source is copied into this repository. Run these commands from the
parent directory that contains all three checkouts:

```powershell
cd .\esp-sdr
git submodule update --init components/esp-dsp
git apply ..\nes-sdr\integration\esp-sdr\0001-expose-last-spectrum.patch

$env:ESP_SDR_SOURCE_ROOT = (Get-Location).Path
$env:NES_SDR_SOURCE_ROOT = (Resolve-Path ..\nes-sdr).Path
cd ..\fc-rom-vomitter\firmware
pio run -e esp32-s3-nes-sdr-synthetic
pio run -e esp32-s3-nes-sdr
```

The two environment variables are converted to CMake paths on Windows.
`ci/nes-sdr.defaults` carries the S3 RF ring settings, including unicore mode
and disabled interrupt watchdog. The link guard rejects any firmware whose
static RAM or IRAM would overlap the RF ring. Do not remove that guard to make
a build pass.

ESP-SDR is GPL-3.0 licensed. A distributed binary that links this component
must meet its license terms. Check the licensing of all linked sources before
public firmware distribution.

## Operator sequence

1. Complete the existing first-board power, SRAM, and bus-isolation gates.
2. Build the NES-SDR display ROM from its source; do not commit or publish the
   generated ROM in this repository. Upload it through the normal USB protocol.
3. Power the Famicom, wait for READY, press the console RESET button, and
   visually confirm the stable NES-SDR screen. The marker check identifies
   the ROM, but cannot prove that its CPU has reached the internal-RAM loop.
4. Send the four ASCII bytes `RVLA` to the cartridge's native USB Serial/JTAG
   port. `RVOK live_armed` confirms the manual arm. Any `RVER live ...` response
   means the task remains disarmed.
5. Start with `esp32-s3-nes-sdr-synthetic`: the graph cycles through flat,
   left, center, right, and full patterns at roughly 1 Hz. Confirm each frame
   without RESET and verify that static font and axis tiles remain intact.
6. Scope `LOAD_MODE`, RUN, `CHR_WE_n`, `CHR_OE_n`, the MCU enable lines, and a
   PPU-side data line during repeated cycles. Confirm no overlapping bus
   drivers. Record the longest LOAD interval and a long-run count.
7. Only then use `esp32-s3-nes-sdr` for 75 ms local 16 MS/s, FFT-256 capture.
   Capture ends before CHR ownership changes. Confirm frequency ordering and
   update rate with a known RF source.

An image install or console-power transition disarms live refresh. The task
holds the controller mutex from capture through CHR readback, so concurrent
uploads and power callbacks cannot take the SRAM bus during RF capture. A CHR
verify failure leaves LOAD asserted and disarms the task. Recover by resolving
the fault, allowing the stored image to reload, pressing RESET, and arming
again. Do not flash either candidate or insert an untested board into a
Famicom before the bench gates above.
