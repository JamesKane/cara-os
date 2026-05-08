# Phase 1 Subgoal 4 — Framebuffer (Dath)

> Retrospective plan / status doc. The first-cut Dath framebuffer
> module shipped before this document was written; this file
> consolidates what landed, names the contracts the rest of Phase 1
> can rely on, and scopes what's deliberately deferred to Phase 4.
>
> Pairs with `docs/ROADMAP.md` Phase 1 Subgoal 4 (the one-line
> requirement), `docs/PHASE1_RUNTIME.md` (Tier 1–3 substrate this
> module sits on), and `docs/ARCHITECTURE.md` §11 (the
> brand-vs-API namespace split — `Dath` is the brand-namespace
> implementation that Phase 3's `graphics.library` will eventually
> trampoline into).

---

## Status — 2026-05-08

**First cut shipped.** Dath provides a CPU-only framebuffer
abstraction sufficient for Croi to draw a boot banner, render
structured log output to screen as a regular `LogSink`, and serve
as the substrate Phase 4's RTG-style GPU driver will plug under.
On QEMU `-machine virt -nographic` (the daily driver) there is no
framebuffer node and Dath logs `headless boot`; the synthetic
in-heap host unit test (`test_dath_fdt`) plus the kernel-side
`dath_smoke` exercise the primitives end-to-end without a real FB.

What landed:

- **`DathFramebuffer`** (RGBA8888 / BGRA8888 / RGB565 formats).
- **simple-framebuffer FDT discovery** —
  `Dath_Framebuffer_FromFdt` parses `/chosen` or `/reserved-memory`
  for an active `simple-framebuffer` node and returns a
  `DathFbDescriptor`.
- **Pixel + clipping primitives** — `Dath_Pixel`, `Dath_FillRect`,
  `Dath_Clear`, `Dath_BlitRect`. Full clipping for negative origins,
  oversize spans, and partially off-screen rects.
- **Vector primitives** — `Dath_DrawLine` (Bresenham, all eight
  octants) and `Dath_DrawRect` (outlined rectangle = four
  `DrawLine`s).
- **Glyph + text** — `Dath_DrawChar` / `Dath_DrawString` over an 8×8
  bitmap font covering space, `!`, `,-./:`, digits 0-9,
  uppercase A-Z, lowercase a-z. Unmapped slots render blank, never
  garbage.
- **`DathConsole`** — wraps a framebuffer + font into a
  cursor-tracked text surface with newline / carriage-return / soft
  tab handling and auto-scroll-up via `Dath_BlitRect`. Drop-in
  `LogSink` (`Log_Sink_DathConsole_Emit`) — once the FB is up,
  every `Croi_Log` line at INFO+ renders to screen alongside the
  UART.
- **Bitmap allocator** — `Dath_AllocBitmap` / `Dath_FreeBitmap` over
  the kernel heap. Provides an off-screen surface for blits and an
  AmigaOS `BitMap`-shaped object for Phase 3's `graphics.library`
  to forward into.
- **Boot path** — `croi_entry` probes the FDT for
  `simple-framebuffer`, on success clears the framebuffer dark blue,
  draws a 96×96 lighter-blue boot pattern, and registers the FB
  log sink. Headless QEMU virt logs `no simple-framebuffer in FDT`
  and continues normally.
- **Tests.** `cara_dath` is dual-target so a host unit test
  (`test_dath_fdt`) verifies the simple-framebuffer FDT parser
  without a kernel; `dath_smoke` (kernel-side) exercises every
  primitive against a synthetic in-heap framebuffer.
- **`tools/font-gen/`** — Python utility that converts a TTF/OTF
  outline font at a fixed pixel size into the C source for an 8×8
  bitmap font. The Phase 1 8×8 ASCII font is checked in as the
  output of one canonical run.

What's deliberately *not* shipped here (Phase 4 territory):

- GPU acceleration — no OpenGL, no Vulkan, no command ring. CPU
  blits only.
- RTG-style driver vector table (`drv_LoadView`, `drv_MakeVPort`,
  …) — that's the Phase 4 hookup model.
- The full V36+ `graphics.library` surface (`RastPort`, `Layer`,
  `View`, `ViewPort`, sprites, the area-fill / text / blit LVOs)
  — Phase 3 wraps Dath through that surface; the Phase 1 cut
  leaves the brand-namespace primitives directly callable from
  kernel code.
- Multi-monitor / multi-display.
- HAM / EHB / PLUT pixel-type rendering — not relevant on RV2's
  true-colour SoC GPU, deferred.

---

## Context

`docs/ROADMAP.md` Phase 1 Subgoal 4 says:

> A simple framebuffer Clar can draw into. v0 inherits whatever
> U-Boot set up (the FDT will expose it as a `simple-framebuffer`
> node when present); a from-scratch DPU / display-controller
> bring-up is **out of scope for Phase 1** and deferred to Phase 4.

Dath therefore had two driving constraints:

1. **No display-controller bring-up.** Whatever pixel layout U-Boot
   leaves us in is what we draw into. The FDT
   `simple-framebuffer` binding documents the byte format; we
   trust it.
2. **Big enough for Clar.** Clar (Phase 1 Subgoal 7) needs blitting
   (drawer icons, window decorations), text (Inntin / labels), and
   full clipping (windows can be partially off-screen). Without
   those, Clar can't be written. With those, the rest of the
   surface (acceleration, RTG vector table) is a pure performance
   /API-shape concern that lives in Phase 4.

The structural decision Dath made in v0 was to *not* try to be
`graphics.library` yet. Phase 3 will publish the canonical V36+
RastPort/BitMap/View API on top of Dath; Phase 1 just exposes
the brand-namespace primitives. That keeps the v0 surface honest
about what it is — a CPU rasteriser — and avoids guessing at
Phase 3's eventual struct shapes.

---

## Module layout

```
include/cara/dath.h          public Dath surface (brand namespace)
src/croi/dath/
  framebuffer.c              DathFramebuffer + format enum + clipping
  fdt_simple.c               simple-framebuffer FDT discovery
  draw.c                     Pixel / FillRect / Clear / BlitRect / lines / rects
  text.c                     DrawChar / DrawString / measure
  font_8x8.c                 generated 8×8 ASCII font glyph table
  bitmap.c                   AllocBitmap / FreeBitmap
  console.c                  DathConsole + Log_Sink_DathConsole_Emit
tools/font-gen/              host-side TTF → C-source 8×8 font generator
tests/unit/test_dath_fdt.c   host test for fdt_simple
src/croi/tests/test_dath.c   kernel-side dath_smoke
```

The `cara_dath` static library builds for both the rv64 kernel and
the host, controlled by `src/CMakeLists.txt`. The host build skips
the framebuffer-write paths (no kernel page allocator) and exercises
the FDT parser only.

---

## Contracts the rest of Phase 1 / Phase 3 relies on

The Phase 1 Subgoals after this (USB → Leargas → Clar) and Phase 3's
`graphics.library` together depend on the following invariants:

1. **A `DathFramebuffer` is a self-describing surface.** It
   carries width, height, stride (in bytes), pixel format, and a
   write pointer (kernel-VA). Any caller can write into it without
   first asking what the pixel layout is — `Dath_Pixel`,
   `Dath_FillRect`, etc. dispatch on the format internally.
2. **Clipping is the caller's right, not their responsibility.**
   Every primitive accepts arbitrary integer coordinates including
   negative origins and over-the-edge spans, and silently clips to
   the framebuffer's extent. Callers can pass through user-supplied
   geometry without guarding it.
3. **The boot framebuffer is just a `DathFramebuffer`.** Once the
   FDT discovery has populated a `DathFbDescriptor`, the rest of
   Croi treats the boot framebuffer identically to a heap-allocated
   off-screen bitmap. There is no special-case "the screen" path.
4. **Text rendering is a primitive, not a library.** The 8×8 font
   plus `Dath_DrawChar`/`String` is sufficient for the boot banner,
   the FB log sink, and Clar's labels. Phase 3's `diskfont.library`
   adds bitmap- and outline-font *loading*; the rendering primitives
   stay here.
5. **`Dath_AllocBitmap` returns an AmigaOS-`BitMap`-shaped object.**
   Phase 3's `graphics.library` `AllocBitMap` LVO wraps this with
   the canonical V36+ field set; the underlying storage is a
   regular Dath surface. No format conversion at the seam.

---

## Out-of-scope scenarios that came up during v0 and got deferred

- **Pointer (mouse) rendering.** Lives with Phase 1 Subgoal 6
  (Leargas / Intuition) — the pointer is logically a sprite, not a
  framebuffer primitive. Dath provides the
  `Dath_AllocBitmap`/`Dath_BlitRect` substrate Leargas will use.
- **Vsync / WaitTOF / WaitBeam.** No display-controller access in
  Phase 1 means no scanout-position hook. Stub'd until Phase 4
  brings up the DPU.
- **Damage tracking.** Every primitive is "draw and it's there";
  there is no dirty-rect collection or compositor seam. Will arrive
  with Phase 4 + Phase 6 (hi-DPI desktop) work.
- **Format conversion.** RGBA8888 ↔ BGRA8888 ↔ RGB565 conversion is
  per-primitive (each draw routine knows the output format); we
  don't synthesise an intermediate format. Adequate for the three
  shipped formats.
- **Multiple framebuffers.** The boot path registers one FB log
  sink and Dath supports as many `DathFramebuffer` instances as the
  caller allocates, but Phase 1 has only one screen. Multi-screen
  arrives with Phase 3's `OpenScreen` LVO and Phase 4's mode
  database.

---

## What this unblocks

- **Phase 1 Subgoal 6 (Leargas).** Pointer rendering, window
  decoration drawing, focus-highlight redraws, IDCMP refresh
  events all use Dath primitives directly.
- **Phase 1 Subgoal 7 (Clar).** Background screen fill, drawer-icon
  blits, Inntin (text-input gadget) drawing, single-line text
  cursor blink — every visual element of Clar is one of the
  primitives above.
- **Phase 3 `graphics.library`.** The lvo-gen `.conf` for
  `graphics.library` declares its LVOs as `local`-flavour against
  the existing `Dath_*` symbols (where the operation is purely CPU
  on a SASOS-resident surface) or `server`-flavour against a
  future Dath Gleas (where the operation needs to be serialised
  against the GPU's command ring — Phase 4). The Phase 1 cut
  ships the `local` impls; Phase 4 adds the `server`-flavour
  marshalling.
- **Phase 4 GPU driver.** When the X1 GPU driver lands, it
  registers via the RTG-style vector table inside
  `graphics.library` and overrides specific operations with
  hardware-accelerated paths; the Dath CPU rasteriser stays as
  the fallback. No Phase 1 code needs to change.
