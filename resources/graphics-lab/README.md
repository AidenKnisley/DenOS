# DenOS graphics validation corpus

This directory is a real regression/reference pack for the retained kernel graphics library and QOI decoder. It is deliberately separate from the boot filesystem: normal CLI boot does not load these large developer fixtures into kernel RAM.

## Fixture format

Each scene has two files:

- `.qoi`: a 1600 x 900 QOI image with four channels. This is the compressed input for `kernel/lib/qoi.c`.
- `.rgba`: the exact row-major 8-bit RGBA reference surface used to validate every decoded pixel. The decoder returns opaque `0x00RRGGBB` pixels, so the test compares R/G/B and retains alpha in the reference for future alpha-aware decoder work.

All data is generated deterministically by `tools/generate_gfx_fixtures.c`; no downloaded or third-party artwork is used. Regenerate with:

```sh
cc -O3 -std=c11 tools/generate_gfx_fixtures.c -lm -o /tmp/gfx-fixtures
/tmp/gfx-fixtures resources/graphics-lab/fixtures
```

Run the full checks (including all ten 1600 x 900 scenes):

```sh
make qoi-fixtures-test
make test
```

## Scene catalogue

| File stem | Purpose |
|---|---|
| `01-color-calibration` | RGB/gray ramps, primaries, secondaries, and black/white endpoint checks |
| `02-pixel-grid` | one-pixel edges, checkerboards, aliasing, and high-frequency sampling |
| `03-rgb-gradients` | channel interpolation, row pitch, and color channel ordering |
| `04-alpha-ramps` | alpha-channel transport and future blend validation; RGB decoder output is separately checked |
| `05-geometry-clipping` | repeated grid lines, circles, clipped edges, and geometry rasterization reference |
| `06-high-entropy-stress` | deterministic worst-case decoder throughput and memory pressure |
| `07-texture-noise` | varied local deltas, QOI diff/luma paths, and deterministic texture patterns |
| `08-palette-atlas` | tile boundaries, repeated colors, and index-table cache behavior |
| `09-ordered-dither` | threshold patterns, quantization, and subtle channel changes |
| `10-scanline-edges` | diagonal boundaries, scanline contrast, and clipping artifacts |

The `.rgba` surfaces intentionally account for most of the uncompressed resource size. They are golden test inputs, not random archive padding. Keep the QOI and RGBA file stems paired and regenerate both together. The host fixture test validates every pixel to detect broken encoders, decoder regressions, channel-order mistakes, and truncated assets.
