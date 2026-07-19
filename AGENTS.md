# Repository guidance

- The native 60-pin Famicom design is the primary release. `experimental/nes/` is a frozen, non-release 72-pin NES snapshot and must remain clearly labeled experimental.
- `hardware/tools/gen_sch.py` is the source of truth for schematic connectivity.
- Preserve the console-facing tongue, J1 datum, notches, centre slot and 1.2 mm thickness.
- Never enable console and MCU SRAM-bus drivers simultaneously.
- Never treat zero opens as release completion; classified DRC, power, visual and CPL gates are independent.
- Do not publish order IDs, account/payment data, addresses, credentials, ROMs or browser screenshots.
- Keep MIT and CC BY-SA scopes explicit when adding files.
