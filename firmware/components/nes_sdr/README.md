# NES-SDR integration component

This directory is an **experimental integration copy** of the platform-independent
firmware module from `Keitark/nes-sdr`.

Source revision at import: `64c781669fe468bf636825d9fd2f692446452c87`.

It is intentionally kept on the `nes-sdr-integration` branch while the
multi-repository ESP-SDR / FC ROM Vomitter integration is validated.

## Licensing scope

FC ROM Vomitter's existing software release is MIT licensed.

NES-SDR does not yet carry an explicit repository license. Therefore these
imported files are **not claimed as part of the FC ROM Vomitter MIT release**
until an explicit NES-SDR license is selected. Keep this distinction when
reviewing or merging the integration branch.

## Purpose

The component contains no GPIO, RF, FreeRTOS, or ESP-IDF-specific logic.

It provides:

- ESP-SDR SPC1 parsing;
- natural-order FFT shift;
- 24-column spectrum reduction;
- 3072-byte CHR graph rendering;
- allocation-free capture -> render -> refresh orchestration.

Platform-specific capture and CHR refresh callbacks are supplied by the
integration application.
