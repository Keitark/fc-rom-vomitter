# ESP32-only two-bank CNROM hardware patch

## Scope and warning

This rework exposes three mapper-observation inputs to the onboard ESP32-S3
and lets GPIO36 drive CHR SRAM A13. It is for an already-built Rev A-FC board.
It is not a fabrication-ready schematic or PCB revision.

Disconnect USB and remove the cartridge from the console before continuity or
soldering work. Confirm every point by KiCad net name and continuity on the
actual board; reference designators alone are insufficient.

## Preferred signal wiring

| ESP32 signal | Source / destination | Preferred implementation |
|---|---|---|
| GPIO36 output | U14 pin 28, CHR A13 | Lift U14 pin 28 from its GND pad; wire TP1 to the isolated lead |
| GPIO42 input | CPU D0 | Lift the unused U2 pin 9 from GND, wire CPU D0 to that lead, then wire U2 pin 11 to U23 pad 35 / GPIO42 |
| GPIO43 input | `ROMSEL_inv` | U19 pin 4 to J3 pin 2 |
| GPIO44 input | `PRG_EN_n` | U21 pin 4 to J3 pin 3 |

U2 is the preferred CPU-D0 path because its spare channel performs the
5 V-to-3.3 V interface on the released board. Do not connect raw CPU D0
directly to an ESP32-S3 GPIO.

GPIO43 and GPIO44 reuse J3 UART pins. Do not attach a UART adapter while the
mapper firmware is running.

## Tested rescue path after U2 lead damage

On the single bench board, U2 pin 9 was lost during rework. The temporary
repair connected CPU D0 through 4.7 kΩ to the GPIO42 node, with 10 kΩ from
that node to GND. U2 pin 11 was disconnected from GPIO42.

```text
CPU D0 ---- 4.7 kΩ ----+---- GPIO42
                       |
                      10 kΩ
                       |
                      GND
```

This topology is recorded because it was the path used for the bench result.
It is **not the recommended reproducible modification**: nominal divider
voltage is close to the upper end of 3.3 V logic, resistor tolerance and the
real console high level matter, and the edge is weaker than with a buffer.
Measure the powered node with the intended console before use. A future PCB
revision should use a specified 5 V-tolerant translator or latch instead.

## Why the qualifier works

U21 combines `ROMSEL_inv`, CPU R/W, and RUN. During RUN, a selected read makes
`PRG_EN_n` low; a selected write keeps it high. The firmware therefore treats
`ROMSEL_inv=1` and `PRG_EN_n=1` as the write candidate and confirms it with an
immediate second GPIO snapshot before committing CPU D0 to GPIO36.

The second sample is important because the beginning of a read can briefly
look like a write while U21 propagates, while a long multi-sample filter was
observed to miss real short writes.

## Rework procedure

1. Remove all power and verify the board is outside the console.
2. Lift U14 pin 28 from GND. Verify the lead is open to GND and not shorted to
   pins 27 or 29.
3. Wire TP1/GPIO36 to the isolated U14 pin 28 lead.
4. Implement the preferred U2 CPU-D0 path above. Use the divider only as a
   documented repair when the buffered path is unavailable and its voltage
   has been measured.
5. Wire U19 pin 4 to J3 pin 2/GPIO43.
6. Wire U21 pin 4 to J3 pin 3/GPIO44.
7. Keep wires short, secure them mechanically, and keep them outside the
   Famicom contact tongue and ESP32 antenna keepout.

## Mandatory checks

1. With power off, confirm the lifted U14 lead is isolated from GND and its
   neighbors.
2. Confirm GPIO42 is not directly connected to the 5 V CPU bus.
3. Confirm all three input points and GPIO36 by continuity to their named
   nets, including correct J3 pin order.
4. Power from a current-limited USB source first; stop for abnormal current or
   heating.
5. Measure GPIO42/43/44 powered levels against ESP32-S3 limits.
6. Verify both U14 8 KiB regions in LOAD mode.
7. Run Mapper 0 and confirm no display or audio regression.
8. Exercise repeated bank 0/1 writes and inspect `/api/status` counters.
9. Save and inspect `/api/mapper-trace` around any remaining mismatch.
10. Treat multi-board timing and long-run Wi-Fi stress as open qualification
    work; a hardware latch remains the robust fallback.
