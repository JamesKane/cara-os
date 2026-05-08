# tools/font-gen

Host-side generator that turns a TrueType / OpenType font into a C
source file matching the `DathFont` shape that `src/croi/dath/text.c`
consumes. The output is one byte per glyph row, MSB the leftmost
pixel — same layout as the hand-coded `src/croi/dath/font_8x8.c` so
swapping is a file replacement, not an API change.

## Three font paths in dath, and where this tool fits

  1. **`src/croi/dath/font_8x8.c` (canonical placeholder).** Hand-coded
     glyphs for ASCII 0x20..0x7A. Lives in the kernel tree, builds
     under cara_dath, has no external dependencies. Always works.
  2. **This generator** — point it at a TTF/OTF and overwrite
     `font_8x8.c` (or emit a sibling). Useful while we wait for a
     real font loader: gives us a wider character set without each
     glyph being typed by hand.
  3. **Phase 7 TrueType loader.** Parses an OpenType file embedded in
     the kernel image (or a follow-on user resource), rasterises
     glyphs at runtime. That work obsoletes both 1 and 2.

The kernel-internal `Dath_DrawChar` / `Dath_DrawString` API doesn't
change across these — `struct DathFont` stays the same brand-namespace
shape (per `docs/PRINCIPLES.md` §3.1; the AmigaOS-style userspace
trampolines that wrap these into `graphics.library` calls are Phase 3
work).

## Recommended source fonts

We don't ship a font with Cara. Pick one yourself, mind the licence
(BSD-2-Clause / CC0 / public-domain only — no proprietary embed):

  * **Topaz / Topaz Plus** by Bert Govaerts. Amiga ROM font remade as
    TTF; several free downloads exist on font archives. Closest to the
    AmigaDOS aesthetic Cara is going for.
  * **Amiga4ever** by ck. Topaz-inspired pixel font, freely usable.
  * **Pixel Operator** by Jayvee Enaguas. Generic 8x8 / 8x16 pixel
    font, CC0 on the author's site (https://www.dafont.com/pixel-operator.font
    — confirm licence before bundling).

Drop the font into `tools/font-gen/` (the directory's `.gitignore`
keeps `*.ttf` / `*.otf` out of the tree) or pass an absolute path.

## Dependencies

Pillow (PIL fork). Standard `pip install Pillow` works on macOS
Homebrew Python and most Linux distributions:

```
$ pip install Pillow
```

Pillow uses FreeType under the hood, which gives bit-exact rendering
of pixel fonts at small sizes.

## Usage

Regenerate `src/croi/dath/font_8x8.c` from a Topaz TTF:

```
$ ./tools/font-gen/font_gen.py \
      --font ./tools/font-gen/Topaz.ttf \
      --size 8 \
      --first 0x20 --last 0x7A \
      --name dath_font_8x8 \
      --output src/croi/dath/font_8x8.c
wrote 91 glyphs to src/croi/dath/font_8x8.c
```

Inspect the diff (`git diff src/croi/dath/font_8x8.c`) before
committing — pixel-font rendering at the wrong `--size` looks
nothing like the source, and it's easy to accidentally produce
all-zero glyphs if Pillow can't find the right metrics.

## Flags

| flag           | default          | notes                                            |
|----------------|------------------|--------------------------------------------------|
| `--font`       | (required)       | TTF/OTF input                                    |
| `--size`       | (required)       | Pillow font size; for true pixel fonts try the same value as `--height` |
| `--width`      | `8`              | glyph width in pixels; ≤ 8 (Tier 1 stores one row per byte) |
| `--height`     | `8`              | glyph height in pixels                           |
| `--first`      | `0x20`           | first codepoint                                  |
| `--last`       | `0x7A`           | last codepoint inclusive                         |
| `--name`       | `dath_font_8x8`  | emitted DathFont symbol                          |
| `--threshold`  | `128`            | alpha threshold for set vs clear                 |
| `--output`     | (required)       | C source path                                    |

## Output

The emitted file:

  * Has a generated-at date in the header.
  * Includes `<cara/dath.h>` and `<cara/types.h>` — same as the
    hand-coded fallback.
  * Defines `static const u8 NAME_glyphs[]` and
    `const struct DathFont NAME`, fully populated from `--first` to
    `--last`.
  * Each row is one byte, comma-separated, with a `// 0xCC 'x'`
    comment per glyph for human review.

Re-run on every codepoint range or font swap. Hand edits should go
into the generator (or wait for the TrueType loader that supersedes
both).
