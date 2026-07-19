# First-board bring-up

> Do not insert an untested board into a Famicom.

## Equipment

- current-limited USB supply or bench supply;
- multimeter;
- oscilloscope or logic analyzer;
- thermal camera or contact thermometer if available;
- inexpensive mapper-0 homebrew test ROM whose rights permit use.

## Bench sequence

1. Inspect the assembled board under magnification for bridges, reversed parts, connector damage and debris.
2. Check resistance from 5 V and 3.3 V to GND before applying power.
3. Power from USB with a conservative current limit and no console attached.
4. Confirm 5 V/3.3 V rails, idle current and regulator temperature.
5. Flash the firmware through native USB. If automatic download mode fails, hold **BOOT**, tap **ESP RST**, release **BOOT**, and retry.
6. Confirm LED patterns and the Wi-Fi upload page at `http://192.168.4.1`.
7. Probe `LOAD_MODE`, MCU buffer enables, console buffer enables, `/WE` and `/OE`. Prove that both bus owners are never active together.
8. Run walking-bit and address tests on both SRAMs, followed by byte-for-byte image readback.
9. Interrupt an upload and confirm that the previous committed flash slot still boots.
10. Verify USB power cannot appear on the console 5 V fingers.
11. Measure current transients and regulator temperature during Wi-Fi upload and SRAM load.

## First console test

Only after the bench sequence passes:

1. Power everything off.
2. Insert the board using a mechanically checked custom shell/support.
3. Power the Famicom.
4. Wait for solid blue READY.
5. Press the **console's red RESET button**.
6. Check video, controls, audio continuity through R20, mirroring and repeatable cold boots.

Record board revision, firmware commit, supply voltage/current, temperatures,
waveforms and the ROM hash. A single successful game boot is not full validation.
