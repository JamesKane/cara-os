<!-- SPDX note: markdown is licence-exempt (docs/PRINCIPLES.md §1). -->
# Dath / graphics.library — Phase 3 L4 design

> The L4 scoping doc, in the shape of `docs/LOGAIC_DOS.md` (L3). Read it
> before cutting L4 code. Pairs with `docs/ARCHITECTURE.md` (system
> design), `docs/PRINCIPLES.md` (the rules), `docs/LVO.md` (dispatch),
> and `docs/PHASE3.md §L4` (the one-paragraph charter this expands).
>
> Brand-vs-API (`PRINCIPLES.md §3.1`): the impl module is **Dath**
> (`src/croi/dath`, Irish for *colour*); the shipped binary is
> `graphics.library` with **verbatim V36+** names (`RastPort`, `BitMap`,
> `Move`, `RectFill`, `Text`, `GfxBase`). The two meet only at the LVO
> trampoline.

---

## 1. Scope

L4 turns the existing **Dath CPU rasteriser** (`include/cara/dath.h`,
`src/croi/dath/*` — pixel/line/rect/blit/8×8-text against a
`struct DathFramebuffer`) into the V36+ **`graphics.library`** API: the
`RastPort` drawing model, `BitMap`/`ViewPort`/`ColorMap`, fonts, and the
blits `intuition` + the paint app use.

**In scope (apps-driven):**
- Library construction: `GfxBase`, the `.conf`, boot `MakeLibrary`.
- `BitMap` allocation + `RastPort` init: `AllocBitMap`/`FreeBitMap`,
  `InitRastPort`, `SetRast`.
- Pen + primitives: `SetAPen`/`SetBPen`/`SetDrMd`, `Move`/`Draw`,
  `WritePixel`/`ReadPixel`, `RectFill`.
- Blits: `BltBitMap`/`BltBitMapRastPort`, `ClipBlit`, `ScrollRaster`.
- Text: `OpenFont`/`CloseFont`/`SetFont`/`Text`/`TextLength`, the
  `GfxBase` default font.
- Areas (paint): `InitArea`/`AreaMove`/`AreaDraw`/`AreaEnd`, `Flood` —
  **only if** the chosen paint app exercises them (apps-gated).

**Out of scope (deferred / later phase):**
- **The GPU binding** — Phase 4 (`ROADMAP §Phase 4`). L4 is the canonical
  API + the **CPU rasteriser**; Phase 4 vectors the X1 GPU under the same
  API via the RTG driver model. The CPU path is what L4 ships.
- **Sprites / GELs / Copper / VBlank** — chipset features; CaraOS has no
  chipset (`ROADMAP`). Declared as stubs only if an app links them.
- **Real layer/clip regions** (`layers.library`) — Phase 1 windows are
  tiled, not overlapping; `ClipBlit` clips to the BitMap rect in v0.
  Layered clipping arrives with L5/L7 if window overlap needs it.
- **Planar Chip-RAM semantics** — see §2.1.
- `OpenScreen`/`OpenWindow` — those are **`intuition.library` (L5)**;
  L4 provides the `ViewPort`/`ColorMap`/`BitMap` substrate they build on.

**The apps that drive L4** (`PHASE3.md §3`): **intuition** (text, the
blits behind window chrome) and **paint** (RastPort draw/fill/blit, and
areas if used). The editor + file-manager only need text, which intuition
already drives. So the L4 done-bar is: *a Gleas opens `graphics.library`,
allocates a BitMap, draws lines/rects/text/blits into it, and reads the
pixels back correctly* — plus ABI-complete declaration + stub coverage.

---

## 2. The key decisions

### 2.1 Chunky RTG bitmaps, not planar Chip RAM

The V36 `struct BitMap` is **planar** (`Planes[8]` bit-plane pointers,
`Depth` planes of 1bpp). CaraOS has no chipset and no Chip RAM; Dath
surfaces are **chunky linear** framebuffers (RGBA8888 / BGRA8888 /
RGB565), and Phase 4 is explicitly **RTG** (`ROADMAP §Phase 4`). So
CaraOS graphics is chunky from the start — the same deviation CyberGraphX
/ RTG made on real Amigas.

**Resolution — `struct BitMap` is ABI-shaped but opaque, head of a
kernel-private extension** (the L3 `DosLockExt`/`DosFileExt` pattern):

```c
// public, <graphics/gfx.h> — V36 ABI offsets preserved
struct BitMap {
    UWORD BytesPerRow;
    UWORD Rows;
    UBYTE Flags;
    UBYTE Depth;
    UWORD pad;
    PLANEPTR Planes[8];
};

// kernel-private (src/croi/dath), the BPTR-free analogue:
struct DathBitMapExt {
    struct BitMap bm;            // offset 0 — what the app holds
    struct DathFramebuffer surf; // the real chunky pixels
};
```

`AllocBitMap` returns `&ext->bm`; the rasteriser recovers `surf` from any
`struct BitMap *`. `BytesPerRow`/`Rows`/`Depth` are filled best-effort for
source that peeks, but the **real geometry/format lives in `surf`** —
apps treat the BitMap as opaque for chunky surfaces (the RTG idiom; you
query attributes through calls, not by reading `Planes`).

**Consequences, recorded for `PORTING.md`:**
- `AllocBitMap` (technically a V39 LVO) is the **primary** drawable-BitMap
  constructor — adopted because the chunky/RTG model needs it. "V36+" here
  means the V36 API *plus* the RTG allocators, consistent with the
  Phase-4 RTG charter.
- `AllocRaster`/`InitBitMap` (planar) are **declared for ABI** but v0
  rasterises chunky only; a program that pokes `Planes[]` assuming planar
  Chip-RAM layout is chipset-specific and out of scope. The compile-compat
  contract is symbol/struct *names and offsets*, not planar pixel
  semantics.
- Binary compat (offset-exact planar behaviour) is the **Phase-9**
  translator's problem, not L4's.

### 2.2 `syscall`-flavour rasterisation on the in-place kernel rasteriser

Where does a draw call run? Two models:

- **A — `local`:** map the framebuffer + bitmaps into U-mode and relocate
  the rasteriser into the library RX page (self-contained, like
  `utility` tag-ops). Zero syscalls per op.
- **B — `syscall`:** each drawing LVO traps into Croi, which runs the
  **existing kernel-side Dath rasteriser** against the RastPort's BitMap.

**Decision: B for v0.** Reasons:
1. The Dath rasteriser is already kernel code, and **intuition already
   renders kernel-side** (`Leargas_*` calls `Dath_*` today). B reuses all
   of it in place; A would re-relocate it self-contained — the exact
   risk class that bit L1 ("static-inline that doesn't inline →
   PC-rel overflow").
2. The display framebuffer is **kernel-VA** (`g_fb`); B keeps it that way
   (no U-mode framebuffer mapping to plumb).
3. **Phase 4 supersedes the hot path** — the GPU/RTG driver vectors
   drawing into hardware; a CPU `local` fast path would be thrown away.
   The 1080p@60 budget (`PRINCIPLES §4`) is a *Phase-4* obligation;
   Phase 3 ships "a CPU rasteriser" (`ROADMAP`), not the 60 Hz path.
4. Per-call trap cost is fine for window chrome + a paint app under QEMU.

So **all L4 drawing + allocation LVOs are `syscall` flavour**, each a
`Cara_Trampoline_Gfx*` in `.lib_text.graphics` → `Croi_Gfx_*_Impl`
(`src/croi/dath`) → `Dath_*`. (`OpenFont`/pure-math leaf calls like
`TextLength` *could* be `local` later; start uniform.) The `local`
relocation is noted in §6 as a non-goal unless a profiled app demands it.

### 2.3 RastPort lives with the caller; BitMap carries the surface

A `struct RastPort` is small mutable drawing state (current pen, position,
draw mode, font, `BitMap *`). The app owns it — on its U-mode stack or in
the SASOS shared heap — and passes `&rp` to each call. The kernel reads/
writes it through the pointer with **SUM=1** (the same way dos reads a
stack `FileInfoBlock *` today). `InitRastPort` zero-inits it and sets
sane defaults (APen 1, BPen 0, `JAM2`, the GfxBase default font).

The `RastPort.BitMap` must be a real Dath surface: either an off-screen
`AllocBitMap` result (shared heap, app-readable — so a test can read
pixels back) or the **screen BitMap** wrapping the boot framebuffer
`g_fb` (kernel-VA — drawing lands directly on the display). The
rasteriser derives the `DathFramebuffer` from `rp->BitMap` either way, so
one code path serves both off-screen and on-screen targets.

This also lets `struct Screen.RastPort` (today `nullptr`,
`leargas/screen.c`) finally become a real RastPort over the screen
BitMap — see §5 L4.7 (optional convergence).

---

## 3. The BitMap/RastPort ↔ Dath bridge

The whole impl is a thin marshal from V36 graphics calls onto the
existing `Dath_*` primitives. Representative mapping:

| graphics.library LVO            | Dath primitive                          |
|---------------------------------|-----------------------------------------|
| `AllocBitMap`/`FreeBitMap`      | `Dath_AllocBitmap`/`Dath_FreeBitmap` (in a `DathBitMapExt`) |
| `InitRastPort`                  | zero + defaults (no Dath call)          |
| `SetRast(rp,pen)`               | `Dath_Clear(surf, pen→DathColor)`       |
| `SetAPen`/`SetBPen`/`SetDrMd`   | store in RastPort (no Dath call)        |
| `WritePixel`/`ReadPixel`        | `Dath_Pixel` / read `surf` directly     |
| `Move`/`Draw`                   | update `rp_cp_x/y`; `Dath_DrawLine`     |
| `RectFill`                      | `Dath_FillRect`                         |
| `BltBitMap`/`BltBitMapRastPort` | `Dath_BlitRect`                         |
| `ClipBlit`                      | `Dath_BlitRect` (clip = BitMap rect, v0)|
| `ScrollRaster`                  | `Dath_BlitRect` + `Dath_FillRect` edge  |
| `OpenFont`/`SetFont`            | wrap `dath_font_8x8` in a `TextFont`    |
| `Text`/`TextLength`             | `Dath_DrawString` / `n * font->width`   |
| `Flood`/`Area*`                 | new span-fill / edge-list (paint, §6)   |

**Pen → DathColor.** v0 is direct-colour: a RastPort pen value is encoded
straight to the surface format via `Dath_RGB*`. A real `ColorMap`
(palette indices `SetRGB4`/`LoadRGB4` → LUT) is minimal in v0 (single
screen, identity/direct colour); a true CLUT lands when an app needs
indexed colour. `SetAPen(rp, c)` stores `c`; the marshal converts to the
target surface's format at draw time.

---

## 4. Library construction (the new-library recipe, graphics specifics)

graphics.library is built exactly like utility/dos (HANDOFF §3 recipe):

- `tools/lvo-gen/graphics.conf` — `##library graphics.library`,
  `##base GfxBase`, `##base_type GfxBase`, `##bias 30`,
  `##owner croi/dath`. Reserved slots 0..3, then the apps-driven rows at
  canonical LVOs with `##pad_run` filling the long tail (the **exec**
  precedent: declare what L4 implements, widen incrementally; the
  unimplemented slots are visible stubs via `lvo-coverage`).
- `include/graphics/gfxbase.h` — **NEW** `struct GfxBase` (LibNode prefix
  at offset 0 + the V36 public fields apps read; `DefaultFont` matters).
  Model it on `intuition/intuitionbase.h` (LibNode + placeholders).
- Fill out the **forward-declared** API headers
  (`graphics/gfx.h` BitMap, `rastport.h` RastPort, `view.h`
  ViewPort/ColorMap, `text.h` TextFont) to their V36 definitions.
- `src/croi/dath/graphics_lib.c` (`Croi_Gfx_*_Impl`) +
  `trampolines.S` (`.lib_text.graphics`) + reserved hooks; whole-archive
  `cara_dath` into croi; `KEEP(.lib_text.graphics)` in `croi.lds`;
  `extern graphics_lib_vec[]` + `Croi_MakeLibrary` in `entry.c`;
  `cara/sysno.h` `SYS_Gfx_*`; add `graphics.conf` to the `lvo-coverage`
  target; `add_dependencies(<app> cara_graphics_lib_gen)`.

**LVO numbers** come from the graphics autodoc / `graphics_lib.fd`. The
generated KB markdown is lost (`CLAUDE.md`); **read `amiga_docs/` to
cross-check, never copy**. Working anchors to verify at `.conf` time
(canonical, classic FD): `BltBitMap -30`, `TextLength -54`, `Text -60`,
`SetFont -66`, `OpenFont -72`, `CloseFont -78`, `InitRastPort -198`,
`SetRast -234`, `Move -240`, `Draw -246`, `AreaMove -252`,
`AreaDraw -258`, `AreaEnd -264`, `InitArea -282`, `RectFill -306`,
`ReadPixel -318`, `WritePixel -324`, `Flood -330`, `SetAPen -342`,
`SetBPen -348`, `SetDrMd -354`, `ScrollRaster -396`, `AllocRaster -492`,
`FreeRaster -498`, `ClipBlit -552`, `BltBitMapRastPort -606`,
`AllocBitMap -918`, `FreeBitMap -924` (the last two V39 — §2.1). The
generator validates `lvo == -(bias + ordinal*6)`, so `##pad_run` counts
keep declaration order aligned with these anchors.

---

## 5. Slice plan (dependency-ordered, each ends green + committed)

- **L4.1 — ABI + GfxBase + the bridge.** `graphics.conf` (full documented
  surface, real rows for L4.2+, rest `##pad_run`); `GfxBase`; fill out
  `BitMap`/`RastPort`/`ViewPort`/`ColorMap`/`TextFont` headers;
  `DathBitMapExt` bridge type; build `graphics.library` at boot.
  *Done when:* a V36 program `OpenLibrary("graphics.library", 36)` and
  reads `GfxBase->LibNode.lib_Version == 36`; coverage row appears.
- **L4.2 — BitMap alloc + RastPort init.** `AllocBitMap`/`FreeBitMap`
  (chunky surface in the shared heap), `InitRastPort`, `SetRast`.
  *Done when:* AllocBitMap a surface, InitRastPort onto it, SetRast fills
  it, and the test reads the cleared pixels back from the shared heap.
- **L4.3 — pen state + primitives.** `SetAPen`/`SetBPen`/`SetDrMd`,
  `Move`/`Draw`, `WritePixel`/`ReadPixel`, `RectFill`. *Done when:* draw
  a line, a filled rect, and a pixel; ReadPixel + direct buffer reads
  match expected colours.
- **L4.4 — blits.** `BltBitMap`/`BltBitMapRastPort`, `ClipBlit` (rect
  clip), `ScrollRaster`. *Done when:* blit a region between two bitmaps
  and verify the destination; scroll a rect and verify the vacated edge.
- **L4.5 — text + fonts.** `OpenFont`/`CloseFont`/`SetFont`/`Text`/
  `TextLength` over `dath_font_8x8` as the system `TextFont`; GfxBase
  `DefaultFont`. *Done when:* `Text` renders a known string into a BitMap
  and the glyph pixels match; `TextLength` returns `len * width`.
- **L4.6 — areas / flood (apps-gated, optional).** `InitArea`/`AreaMove`/
  `AreaDraw`/`AreaEnd`, `Flood`. Only if the paint app exercises them;
  needs a span/edge-list fill the current Dath set lacks. *Done when:*
  fill a triangle via the area list and verify the interior.
- **L4.7 — convergence (optional, may slip to L5).** Repoint Leargas
  chrome rendering through real `graphics.library` RastPorts so
  `Screen.RastPort` (today `nullptr`) is a live RastPort over the screen
  BitMap, and intuition draws via gfx instead of calling `Dath_*`
  directly. Pure refactor; no app-visible change (SASOS).

**L4 done (epic):** `PHASE3.md §5` for every library — (a) `.conf`
declares the documented set at canonical numbers; (b) headers + a
canonical V36 snippet compile/link; (c) the apps-driven primitives have a
`KERNEL_TEST` and/or host test; (d) every unimplemented LVO is a logged
stub the coverage report lists.

**Testing.** Rendering to the live framebuffer is hard to assert; the
primary test is a **userexec-style Gleas** that opens graphics.library,
`AllocBitMap`s an **off-screen chunky surface in the shared heap**, draws
into it, and reads the pixels back to assert exact colours — deterministic
and screen-independent. The Dath pixel math already builds host-side, so
the low-level primitives keep their host unit tests; L4 adds the
library-wiring `KERNEL_TEST`(s).

---

## 6. Open questions / deferred

1. **`local` rasterisation.** Pulling drawing into the library RX page
   (map framebuffer + bitmaps U-mode, relocate the rasteriser
   self-contained) removes the per-op syscall. Deferred: Phase 4's GPU
   vectoring supersedes the CPU hot path, so a `local` CPU fast path may
   never pay for itself. Revisit only if a profiled Phase-3 app needs it.
2. **ColorMap / indexed colour.** v0 is direct-colour (pen → surface
   format). A real CLUT (`GetColorMap`/`SetRGB4`/`LoadRGB4` + an index→
   RGB LUT and indexed surfaces) lands when an app wants a palette.
3. **Areas / Flood (L4.6).** Span/edge-list fill is new code the current
   Dath set lacks; gate on the paint app actually using it. If paint uses
   only RectFill + blits, L4.6 stays a declared stub.
4. **Layer/clip regions.** `ClipBlit` clips to the BitMap rect in v0
   (windows are tiled). True overlapping-window clipping pulls in
   `layers.library` — defer until L5 window overlap needs it.
5. **Fonts beyond 8×8.** `dath_font_8x8` is the only face (uppercase +
   a little punctuation, `dath.h`). `diskfont.library` (L10–L14) and a
   fuller ROM font are apps-driven later; `OpenFont` returns the system
   font regardless of `TextAttr` in v0 (logged).
6. **Planar `AllocRaster`/`InitBitMap`.** Declared for ABI; v0 rasterises
   chunky only (§2.1). Decide per app whether any planar shim is needed
   (probably none — the apps are RTG-written or use AllocBitMap).
7. **`WaitBlit`/`OwnBlitter`/blitter-queue calls.** No blitter hardware;
   these become no-ops/stubs (CPU blits are synchronous). Declare as the
   apps reference them.
