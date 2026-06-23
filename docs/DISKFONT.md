# L12 — diskfont.library (scope)

Cleanroom V36+ `diskfont.library` for CaraOS Phase 3. The library that
loads a font from disk into a `struct TextFont` so the rest of
graphics.library (`SetFont`/`Text`/`TextLength`) can use it. Today the
system has exactly one face — the ROM-embedded `dath_font_8x8`, which
`graphics.library OpenFont` returns for *any* `TextAttr` (it ignores the
request entirely, `docs/DATH_GRAPHICS.md §5`). L12 makes a second,
disk-loaded font real.

This is the middle of the L11–14 long tail (icon ✓, **diskfont**,
commodities, expansion). It is **apps-driven** (`docs/PHASE3.md §3-4`):
the **paint** app's text tool is the sole representative consumer
("`diskfont` (text tool)"); the editor and file-manager need only topaz.
So diskfont gets the surface paint exercises — load a font, list the
available fonts for the asl font requester — and the rest is ABI-complete
and stubbed.

A disk font is **worthless unless it can draw**, and Dath's text renderer
currently hardwires the 8×8 glyphs. So L12 necessarily includes a
graphics.library change: render from a generic `TextFont` strike. This is
the "standing deferred substrate an app forces forward" pattern — an L4
follow-on triggered here.

Read alongside: `docs/DATH_GRAPHICS.md` (the font system + Text path),
`docs/ICON.md` (the most recent scope doc — same shape, same xattr-era
"new format" discipline), `docs/LVO.md` (flavours).

---

## 1. Scope

**In scope (gets a working body):**

- A CaraOS-native bitmap **Cara font file** format (`§2.2`) — a flat,
  target-neutral serialisation of a `TextFont` strike (no 68k hunk /
  `LoadSeg`).
- Dath text rendering from a **generic `TextFont` strike** (`§2.3`):
  `Text`/`TextLength` read `tf_CharData`/`tf_Modulo`/`tf_CharLoc`/
  `tf_XSize`/`tf_CharSpace` instead of the hardwired 8×8 — the built-in
  topaz is reframed as a strike, and a loaded font draws.
- `diskfont.library` base + dispatch (a base-ful `syscall` library, the
  icon/iffparse recipe).
- `OpenDiskFont(textAttr)` — find + load `FONTS:<name>/<ysize>` into a
  heap `TextFont`; `graphics.library CloseFont` frees a disk font.
- `AvailFonts(buf, size, flags)` — enumerate the fonts under `FONTS:`
  (memory + disk), the data the asl font requester needs.
- `NewFontContents` / `DisposeFontContents` — the `.font` directory
  helper used by AvailFonts.

**Out of scope (declared ABI-complete, defined stub):**

- **Outline / vector fonts** (`bullet.library`, `.otag`) — bitmap strikes
  only in v0. Paint's text tool uses bitmap faces.
- **`NewScaledDiskFont`** real scaling — stubbed (returns nullptr / the
  unscaled font). Integer bitmap scaling is a later sub-slice if paint
  forces it.
- **`ColorTextFont`** (multi-plane colour fonts), `TFontContents`/tagged
  fonts (V44), `GetDiskFontCtrl`/`SetDiskFontCtrlA` (V40).
- **A shared font list across `OpenFont`+`OpenDiskFont`** (`AddFont`/
  `RemFont` registry) — v0 `OpenDiskFont` returns a fresh `TextFont` each
  call; `graphics OpenFont` still only resolves the ROM face. Unifying the
  two behind one font list is deferred.
- **Proportional kerning niceties** beyond `tf_CharSpace`/`tf_CharKern`
  honoured by the renderer.

**Done-bar:** paint (and the test Gleas) can write a Cara font to
`FONTS:`, `OpenDiskFont` it, `SetFont` + `Text` it into a RastPort and get
*that font's* glyphs (not topaz) with the correct advance, and `AvailFonts`
lists it — proven by a Gleas round-trip that renders and inspects pixels.

---

## 2. The key decisions

### 2.1 diskfont loads into a `TextFont`; graphics renders it

The division of labour is the classic one: `diskfont.library` is just a
**loader** — it turns a disk file into a `struct TextFont` (the
graphics.library ABI struct, `include/graphics/text.h`). All *rendering*
stays in graphics.library/Dath. So L12 has two halves: the loader (new,
`src/croi/diskfont`) and a graphics rendering change (`src/croi/dath`,
§2.3). They meet at `struct TextFont`.

### 2.2 The Cara font file — a flat `TextFont` strike, not a hunk

Classic Amiga disk fonts are `LoadSeg`'d 68k binaries (a `DiskFontHeader`
whose `dfh_Segment` is relocated code/data). CaraOS has no hunk loader and
no 68k. So the on-disk font is a **CaraOS-native, target-neutral
serialisation of the bitmap strike** — the same *data* a classic
`TextFont` carries, laid out flat so it loads with a single read + pointer
fix-up (the icon `CaraIconBlob` discipline):

```
CaraFontFile ::= u32 magic (0x434E4654 "CFNT") | u16 version (1) | u16 flags
               | u16 ySize | u16 xSize | u16 baseline | u16 boldSmear
               | u16 modulo            (bytes per strike row)
               | u16 style
               | u8  loChar | u8  hiChar
               | u8  strike[modulo * ySize]         (all glyphs side by side)
               | u32 charLoc[nGlyphs]   ((bitOffset<<16)|bitWidth), classic packing
               | (if flags&CFNT_PROPORTIONAL) u16 charSpace[nGlyphs] | i16 charKern[nGlyphs]
   nGlyphs = hiChar - loChar + 2   (the +1 is the default/undefined glyph)
```

This is exactly the classic strike (`tf_CharData` + `tf_CharLoc` +
optional `tf_CharSpace`/`tf_CharKern`), so `OpenDiskFont` parses it into
one shared-heap allocation holding the `TextFont` + strike + tables and
points `tf_*` at the regions — `CloseFont` is a single free. Bitmap only;
versioned so an outline-carrying v2 is additive.

### 2.3 Dath renders from a generic strike; topaz becomes a strike

For a loaded font to be visible, `Text` must read the `TextFont` strike
rather than indexing the hardwired `dath_font_8x8`. L12 generalises the
Dath text path:

- `Text(rp, s, n)` walks each char `c`: glyph `= (c in [loChar,hiChar]) ?
  c-loChar : hiChar-loChar+1` (default glyph); from `tf_CharLoc[glyph]`
  take `bitOffset`/`bitWidth`; blit `bitWidth` columns × `tf_YSize` rows
  out of `tf_CharData` (row stride `tf_Modulo`) at the pen, in FgPen over
  BgPen; advance by `tf_CharSpace[glyph]` (proportional) or `tf_XSize`
  (monospace).
- `TextLength` sums the same advances (replaces the `count*width` monospace
  shortcut).
- The **built-in topaz is reframed as a `TextFont` strike** — `font_8x8.c`
  becomes (or is wrapped as) a strike with `modulo`, `CharLoc`, etc., so
  the ROM path and the disk path are one renderer. `FPF_ROMFONT` stays on
  the built-in; loaded fonts carry `FPF_DISKFONT`.

This is the load-bearing change and gets its own slice (L12.1) so it can
land + be proven with the *existing* font before any disk I/O.

### 2.4 Fonts live under `FONTS:` on CaraFS, read via dos

diskfont reads font files with `dos.library` (Open/Read/Examine), like
iffparse and icon read their data. The directory convention is the classic
one: `FONTS:<name>.font` is the index (a `FontContentsHeader` listing
available `ySize`s) and `FONTS:<name>/<ysize>` is the Cara font file for
one size. v0 may skip the `.font` index for `OpenDiskFont` (derive the
path `FONTS:<name>/<ysize>` directly from the `TextAttr`) and only build
`FontContentsHeader`s for `AvailFonts`. `FONTS:` resolves to a fixed path
(`SYS:Fonts`) until a real dos assign exists — noted, not blocking.

### 2.5 `syscall` flavour; resolution + load run kernel-side

diskfont is base-ful `syscall` flavour (icon/iffparse recipe): each LVO is
a `Cara_Trampoline_Diskfont_*` that ecalls into Croi, routed to
`Croi_Diskfont_*_Impl`. The impls resolve `FONTS:` paths and read files
via the kernel `Croi_Dos_*_Impl` bridge (the path Lock/Open the icon
impls use), parse the Cara font, and build the `TextFont` in the SASOS
shared heap. The codec (build/parse) is pure → host-unit-testable like the
icon blob.

---

## 3. LVO surface

Bias 30; reserved slots 0–3 (`Open`/`Close`/`Expunge`/`ExtFunc`) are
`local` hooks. **Offsets are the canonical V36+ `diskfont_lib.fd` values;
locked against `amiga_docs/` when `tools/lvo-gen/diskfont.conf` is written
in L12.2** (cross-check, never copy). `##pad_run` keeps declaration order
aligned (ordinal = `|lvo|/6 − 1`).

| LVO | offset | flavour | slice | notes |
|-----|-------:|---------|-------|-------|
| `OpenDiskFont` | -30 | syscall | L12.2 | load `FONTS:<name>/<ysize>` → TextFont |
| `AvailFonts` | -36 | syscall | L12.3 | enumerate `FONTS:` (memory + disk) |
| `NewFontContents` | -42 | syscall | L12.3 | build a `.font` FontContents |
| `DisposeFontContents` | -48 | syscall | L12.3 | free it |
| `NewScaledDiskFont` | -54 | syscall | **stub** | scaling deferred |

Everything past `NewScaledDiskFont` is declared at its canonical LVO and
emitted as a defined stub (`Croi_LvoUnimplemented`) so a V36 program
links. `OpenDiskFont` returning a font depends on the §2.3 renderer (L12.1)
already being in place.

The render change (L12.1) touches **graphics.library**, not diskfont's
conf — it edits `src/croi/dath` Text/TextLength + the built-in font; no new
LVOs (the graphics font LVOs already exist from L4.6).

---

## 4. Slice plan

### L12.1 — Dath renders from a generic `TextFont` strike

- Reframe the built-in topaz (`src/croi/dath/font_8x8.c`) as a `TextFont`
  strike (`tf_CharData`/`tf_Modulo`/`tf_CharLoc`/`tf_XSize`, monospace).
- Rewrite `Croi_Dath_Text` / `Croi_Dath_TextLength` to render/measure from
  the bound `RastPort` font's strike (default-glyph fallback, FgPen over
  BgPen, per-glyph advance).
- **Test:** the existing graphics text kernel/Gleas test still renders the
  same topaz pixels (a pure refactor — no visible change), plus a unit
  test of the glyph-blit math. No diskfont yet.

### L12.2 — diskfont.library + OpenDiskFont

- `tools/lvo-gen/diskfont.conf` (full surface, offsets locked) →
  `proto/diskfont.h` / `diskfont/lvo.h` / `diskfont_vec.c`;
  `include/libraries/diskfont.h` (verbatim `FontContents`/
  `FontContentsHeader`/`AvailFonts`/`AvailFontsHeader`/`DiskFontHeader`
  ABI + `FCH_ID`/`AFF_*`/`MAXFONTPATH`); the `src/croi/diskfont` library
  (base, hooks, trampolines, MakeLibrary, `KEEP(.lib_text.diskfont)`,
  whole-archive, coverage wiring).
- The Cara font codec (`§2.2` build + parse) + `OpenDiskFont(textAttr)`:
  derive `FONTS:<name>/<ysize>`, read it, parse → `TextFont` (FPF_DISKFONT);
  teach `graphics.library CloseFont` to free a disk font.
- **Test (Gleas round-trip):** build a Cara font (a distinct strike — e.g.
  a 6×8 face, or topaz with two glyphs swapped), `Write` it to
  `FONTS:test/8`, `OpenDiskFont({"test.font",8})`, `SetFont`, `Text` it,
  assert the rendered pixels are the loaded font's glyphs (not topaz) and
  `TextLength` matches; `CloseFont`. Needs a Process (path Open/Lock) → a
  Gleas test, not a KERNEL_TEST. Codec build/parse also gets a host unit
  test.

### L12.3 — AvailFonts + FontContents + asl wiring

- `AvailFonts` (walk `FONTS:` with dos Examine/ExNext, emit `AvailFonts`
  records for the ROM face + each disk `.font`), `NewFontContents` /
  `DisposeFontContents` (parse a `FONTS:<name>.font` index).
  `NewScaledDiskFont` stays a stub.
- Wire the asl font requester (L9) to `AvailFonts` so it offers the real
  faces instead of just the Dath 8×8.
- **Test (Gleas):** seed two font files, `AvailFonts` returns both + the
  ROM face; the asl font-requester pre-seam reflects them.

---

## 5. Testing

- **Render math** (L12.1): host unit test of the strike glyph-blit +
  advance; the existing graphics text test stays green (refactor).
- **Codec** (L12.2): host unit test of Cara font build/parse.
- **OpenDiskFont + render** (L12.2): Gleas round-trip (write → open →
  SetFont → Text → inspect pixels → CloseFont); persistence via the
  two-boot smoke.
- **AvailFonts / asl** (L12.3): Gleas enumeration + requester pre-seam.

Each slice ends on the standing gate: host `ctest` green, in-kernel runner
`0 failed`, format-check clean, two-boot QEMU smoke `ok`; commit; regen
`docs/LVO_COVERAGE.md`; handoff/memory follow-up.

---

## 6. Tracked gaps / deferrals

- **Outline/vector fonts** (`bullet.library`, `.otag`, `OFontContents`) —
  bitmap strikes only; paint's text tool uses bitmap faces.
- **`NewScaledDiskFont` scaling** — stub; integer bitmap scaling only if
  paint forces it.
- **ColorTextFont / colour fonts**, **TFontContents / tagged fonts (V44)**,
  **GetDiskFontCtrl / SetDiskFontCtrlA (V40)** — stubbed.
- **A unified font list** behind `OpenFont` + `OpenDiskFont` (`AddFont`/
  `RemFont` registry, so a `graphics OpenFont` finds a disk-loaded face) —
  v0 keeps them separate; `OpenFont` still resolves only the ROM face.
- **`FONTS:` as a real dos assign** — resolves to a fixed `SYS:Fonts` path
  until the assign layer exists.
- **Proportional kerning** beyond honouring `tf_CharSpace`/`tf_CharKern`;
  antialiasing; Unicode beyond the classic 8-bit `loChar..hiChar` range.
