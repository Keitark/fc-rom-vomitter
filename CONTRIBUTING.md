# Contributing

Issues and small, reviewable pull requests are welcome.

1. Open an issue describing the electrical, firmware, mechanical or documentation goal.
2. Keep generator-backed schematic changes in `hardware/tools/gen_sch.py`; include connectivity/ERC evidence.
3. Never accept a PCB candidate because the ratsnest is empty alone. Include raw opens, classified DRC, power checks and visual evidence.
4. Treat MPN, footprint, placement, routing, BOM and CPL changes as release-invalidating until downstream artifacts are regenerated.
5. Do not include ROMs, Nintendo assets, order/account data, addresses, API keys or browser screenshots.

Firmware changes should pass host tests and the ESP32-S3 build. Hardware changes
should identify what was physically verified and what remains a target or assumption.
