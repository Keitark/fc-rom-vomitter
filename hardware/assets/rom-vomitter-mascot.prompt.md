# ROM Vomitter mascot source

- Model: `gpt-image-2`
- Generated: 2026-07-15
- Purpose: source artwork for the Famicom PCB bottom silkscreen

Prompt:

> Create an original funny retro electronic mascot for a PCB silkscreen: a
> round creature with two large, unmistakable X-shaped eyes, leaning forward
> and vomiting rectangular ROM integrated circuits and small pixel/data
> blocks. Pure black line art on a white background, thick connected strokes,
> minimal shapes, high contrast, vector/stencil friendly, readable when
> reduced to about 22 mm wide. No text, no gray, no gradients, no shadows, no
> watermark, and no resemblance to an existing copyrighted character.

`gen_rom_vomitter_logo.py` converts the raster source into a reproducible,
manufacturable KiCad silkscreen footprint. The generated PNG itself is not
sent directly to fabrication.
