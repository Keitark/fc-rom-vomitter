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

The RF task targets **5 Hz** (200 ms/frame) with a 50 ms capture window.
The GPL ESP-SDR backend is supplied by the separate `Keitark/nes-sdr`
combined-build workflow and is not vendored in this branch. Firmware built
from this branch alone reports the RF backend as unavailable.

The RF task holds the controller lock from capture through CHR readback, so
uploads and console power callbacks cannot change SRAM ownership mid-frame.
Each attempt logs capture, estimated render, refresh, total, and frame period in
microseconds. The SRAM log records CHR write, verify, and console blackout
times; a frame over 200 ms is flagged. These logs are instrumentation, not
hardware timing evidence.

SDR startup is serviced by the resident USB loader task (or `app_main` when USB
is disabled). The HTTP handler only marks the request as starting. After a
500 ms response grace period, the service stops HTTP and setup Wi-Fi before
allocating the 8 KiB RF task stack. This avoids requiring an additional large
contiguous block while setup resources are still allocated. If teardown or task
allocation fails, startup returns to the setup AP/HTTP page with RF state
`error`, allowing another attempt. Status includes `heap_free` and
`heap_largest`; serial logs show both before and after teardown.

## First hardware test

1. Build/flash this branch.
2. Upload the NES-SDR NROM from `Keitark/nes-sdr`.
3. Press the Famicom RESET button once so the ROM initializes the PPU and parks
   the 2A03 at `$0200` internal RAM.
4. Open the ROM Vomitter web UI.
5. Press **NES-SDR demo frame**.
6. The spectrum bars should change without another Famicom RESET.

Each click advances the synthetic peaks by three FFT bins; a visible column
change may take several clicks.

## Read-only USB status

With a firmware containing the serial diagnostic command, send `RVST\n` over
USB or run `python firmware/tools/serial_status.py --port COM3`. The tool keeps
DTR/RTS low and does not reset the board. Close other serial monitors first.

Three lines report:

- `RVST`: RF state, completed RF attempts, successful RF refreshes, last result
  (`0` means success, `-1` means no completed RF cycle), most recent period in
  microseconds, and CRC of the last successfully written graph.
- `RVAP`: current Wi-Fi mode and associated AP station count. `null` means no
  AP. `clients=-1` plus an error means the query failed, not zero clients.
- `RVROM`: installed-image, detected console-power and bus-exposure flags,
  image identity, free heap and largest block.

Only increasing `frames_ok` establishes continued successful RF capture,
render and verified CHR writes. Demo clicks do not increment these counters.
Around `period_us=200000` is the 5 Hz target. This still does not establish that
the Famicom TV is reading/displaying the graph correctly. Counters are atomic
individual readings, not a single locked transaction. The command works while
the setup AP is offline and never changes radio mode or SRAM ownership.

### Repeat page loads while monitoring USB

With the PC connected to the AP and holding a valid `192.168.4.x/24` address:

```text
python firmware/tools/web_stability.py --port COM3 --output ../web-stability-results
```

This downloads `/` and `/api/status` 30 times at five-second intervals, then
20 times at one-second intervals, using an HTTP session. Concurrent read-only
USB queries record RF/AP/heap status every three seconds. Requests have bounded
timeouts; CSV/JSON record response failures, times and heap ranges. Python
`requests` and `pyserial` are required. This tests HTTP delivery and status
responses; it does not execute browser JavaScript or validate rendering.

Keep raw serial output local: Wi-Fi driver logs can contain client identifiers.
If DHCP fails, resolve that network condition before interpreting HTTP timeouts
as web-server failures. A temporary static Wi-Fi address requires administrator
rights on Windows; record and restore the prior adapter configuration afterward.

## ROM signature gate

The NES-SDR NROM embeds the fixed eight-byte signature `NES-SDR1` at CPU
address `$FFF0` (normalized PRG offset `0x7FF0`).

The demo endpoint verifies that signature before touching CHR SRAM. A normal
game or an older unsigned NES-SDR build is rejected with HTTP 409.

This is still an experimental live-write path, but accidental use on unrelated
ROMs is now blocked.

## Refresh target

The first live RF target is **5 Hz**:

```text
200 ms frame budget
  ~50 ms   RF capture / FFT
  remainder CHR render + write + verify + margin
```

If full 3072-byte write+verify does not fit comfortably, optimize the existing SRAM path or add dirty-tile updates before increasing the rate. The baseline must preserve bus-safety and readback verification.

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
