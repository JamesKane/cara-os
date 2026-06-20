<!-- SPDX note: markdown is licence-exempt (docs/PRINCIPLES.md §1). -->
# Leargas / intuition.library — Phase 3 L5 design

> The L5 scoping doc, in the shape of `docs/LOGAIC_DOS.md` (L3) and
> `docs/DATH_GRAPHICS.md` (L4). Read it before cutting L5 code. Pairs with
> `docs/PHASE1_LEARGAS.md` (the existing window/IDCMP substrate),
> `docs/DATH_GRAPHICS.md` (L4 — the rendering L5 draws through), and
> `docs/PHASE3.md §L5` (the charter this expands).
>
> Brand-vs-API (`PRINCIPLES.md §3.1`): the impl module is **Leargas**
> (`src/croi/leargas`, Irish for *vision/insight*); the shipped binary is
> `intuition.library` with **verbatim V36+** names (`OpenWindowTagList`,
> `SetMenuStrip`, `struct Window`, `IntuiMessage`, `IDCMP_*`).

---

## 1. Scope

L5 widens `intuition.library` from today's 5 LVOs (`OpenWindow`/
`CloseWindow`/`AddGadget`/`RemoveGadget`/`ActivateGadget`, ~6% coverage)
to the window-system surface a real V36 application drives — built on the
**existing Leargas substrate** (windows, gadgets, IDCMP, focus, string
gadgets, input ring, pointer, routers — `PHASE1_LEARGAS.md`) and the **L4
graphics.library** rendering just landed.

**In scope (apps-driven):**
- **Tag openers** — `OpenWindowTagList` (the modern idiom), `ModifyIDCMP`.
- **Window RPort** — a live `graphics.library` RastPort per window so apps
  draw into their window (§2.2).
- **Window ops** — `ActivateWindow`, `MoveWindow`, `SizeWindow`,
  `WindowToFront`/`WindowToBack`, `SetWindowTitles`.
- **Menus** — `struct Menu`/`MenuItem`; `SetMenuStrip`/`ClearMenuStrip`/
  `ItemAddress`; a Leargas menu bar + menu-button routing +
  `IDCMP_MENUPICK` (§2.3).
- **Requesters** — `AutoRequest`/`EasyRequestArgs` (modal dialogs),
  `DisplayBeep` (§2.4).
- **Rendering helpers** — `DrawBorder`/`DrawImage`/`PrintIText`/
  `IntuiTextLength` over graphics.library; `struct Border`/`Image`.
- **Gadget surface widen** — `AddGList`/`RemoveGList`/`OnGadget`/
  `OffGadget`/`RefreshGList`/`RefreshWindowFrame`.
- **Misc** — `CurrentTime`/`DoubleClick`/`ReportMouse`/`ViewPortAddress`.
- **Screens** (apps-gated) — `OpenScreenTagList`/`OpenScreen`/
  `CloseScreen` + `NewScreen` + `SA_*` (a custom screen; most apps use
  the boot Workbench screen via `Screen = nullptr`).

**Out of scope (deferred / later epic):**
- **Rich gadgets** (list/cycle/slider/scroller/checkbox) — **gadtools
  (L8)**, built on **BOOPSI (L7)**. L5 ships the base gadget plumbing the
  existing string/bool gadgets use, not the toolkit.
- **File/font/screenmode requesters** — **asl (L9)**.
- **Layers / overlapping-window clipping + occlusion** — L5 windows are
  non-overlapping (as Phase 1's are); see §2.2.
- **The keymap** — `keymap.library` later; Leargas uses its built-in
  RawkeyToAscii.
- **Custom screen depth / display modes / genlock** — chipset/RTG display
  database is Phase 4; v0 screens are the one boot framebuffer's format.

**The apps that drive L5** (`PHASE3.md §3`): **editor** (window + string
input + menus + IDCMP keys), **paint** (window + menus + RastPort drawing
into the window RPort), **file-manager** (window + menus; its list gadget
+ file requester are L8/L9). So the L5 done-bar: *a V36 Gleas opens a
window via `OpenWindowTagList`, draws into `window->RPort` (graphics.
library), attaches a menu strip and receives `IDCMP_MENUPICK`, and pops an
`AutoRequest` dialog* — plus ABI-complete declaration + stub coverage.

---

## 2. The key decisions

### 2.1 Tag openers map onto the existing Leargas substrate

`OpenWindowTagList(nw, tagList)` does not introduce a new window engine:
it builds a `struct NewWindow` from the `WA_*` tags (using the **L2
utility** tag walkers — `NextTagItem`/`GetTagData`) over the optional
`nw` template, then calls the existing `Leargas_OpenWindow`. New `WA_*`
constants (`WA_Left`, `WA_Width`, `WA_Title`, `WA_IDCMP`, `WA_Flags`,
`WA_Gadgets`, `WA_CustomScreen`, …) land in `intuition.h`.

Convention: the **`*TagList` form is the real LVO**; the varargs `*Tags`
form is a thin C convenience (`<proto/intuition.h>`/a header inline that
passes `&firstTag` to the TagList LVO) — no separate LVO. Same for
`OpenScreenTagList` / `EasyRequestArgs`.

### 2.2 Window RPort — a sub-bitmap view onto the screen (no layers in v0)

Each window exposes `window->RPort`, a live graphics.library RastPort so
apps draw with the L4 calls. The crux is what its BitMap is. Decision:
**a "sub-bitmap" view onto the screen framebuffer** — a `DathBitMapExt`
whose `surf` is the screen surface with:
- `base` offset to the window's content origin
  (`(TopEdge+WBorTop)*stride + (LeftEdge+WBorLeft)*bpp`),
- `width`/`height` = the window's inner size,
- `stride` = the **screen** stride.

So drawing at window-relative `(0,0)` lands at the right screen pixel, and
Dath's bounds-clip (it clips to `surf.width`/`height`) **auto-clips to the
window rect** — window-relative coordinates *and* clipping, for free,
reusing L4. Drawing is immediate (straight to the screen); no backing
store, no composite step.

**What this gives up (v0, documented for `PORTING.md`):** no **occlusion**
— overlapping windows overdraw, because there's no layer stack. That's
acceptable while windows are non-overlapping (as Phase 1's tiled Clar
windows are). True overlapping-window occlusion + damage/refresh needs a
**layers** phase (`layers.library`), which replaces the sub-bitmap with a
per-window layer (backing store + clip regions) under the *same*
`window->RPort` API — apps don't change. (`MoveWindow`/`SizeWindow`
recompute the sub-bitmap; if they expose a hole, v0 just leaves stale
pixels until a redraw — a documented v0 limitation.)

Window chrome + gadgets keep their existing screen-direct Dath rendering
(`PHASE1_LEARGAS.md`); the RPort is the *content* surface, inset by the
borders. Both target the same screen pixels (cf. the L4.7 screen RastPort).

### 2.3 Menus — a new Leargas substrate

Leargas has no menus yet; this is L5's biggest new substrate. The V36
model: `SetMenuStrip(win, menu)` attaches a `Menu` list (each `Menu` has a
`MenuItem` list) to the window; pressing the **menu button** (RMB) drops
the menu bar over the screen title; releasing over an item posts
`IDCMP_MENUPICK` whose `Code` is the packed menu number
(`MENUNUM`/`ITEMNUM`/`SUBNUM`); `ItemAddress(menu, code)` resolves it
back. v0:
- Define `struct Menu`/`MenuItem` (ABI) + the flag/`MENU*`/`NM_*` macros.
- Leargas renders the menu bar (headers from the `Menu` list) and the
  drop-down item box (`MenuItem` text via graphics.library `Text`).
- A new menu-button router (mirrors the existing key/gadget/close
  routers) drives the pick interaction and posts `IDCMP_MENUPICK`.
- v0: text items + checkmark/enable flags; **no** sub-menus, image items,
  or command-key shortcuts (deferred, logged).

### 2.4 Requesters — modal windows over the screen

`AutoRequest`/`EasyRequestArgs` build a small modal window (body
`IntuiText` + a row of `OK`/`Cancel`-style bool gadgets from the format),
run a nested IDCMP loop until a gadget is hit, and return which one. Built
entirely on the existing window + bool-gadget + IDCMP substrate. v0:
single line(s) of text + up to a few gadgets; `DisplayBeep` flashes/〈no-
op-logs〉. `Request`/`EndRequest` (app-supplied `Requester` in a window)
and `struct Requester` are declared; full custom requesters are gated on
an app needing them.

### 2.5 Flavour: `syscall`, tag parsing kernel-side

Leargas is **kernel-resident** (the intuition LVOs already trap to
`Croi_*_Impl` → `Leargas_*`). All L5 LVOs are `syscall` flavour, same as
today's five. Tag lists / `NewWindow` / `Menu` strips are app memory read
kernel-side with SUM=1 (like dos's `FileInfoBlock *`). Anything Leargas
dereferences from another task later (it already requires gadgets/
IntuiMessages in the SASOS shared heap — `clar.c`) keeps that rule. Wide
LVOs (>7 register args) use the **L4.4 `local` marshalling-stub pattern**
(`graphics_blit.c`) — but most intuition LVOs are ≤7 args (tag openers
take a pointer + a taglist pointer), so few if any need it.

---

## 3. The Leargas substrate — reuse vs. new

**Reuse (exists, `src/croi/leargas/`):** input ring, pointer, screen
(+ L4.7 RastPort), window open/close/render/hit-test/link, focus
(active window/gadget), IDCMP build/route/getmsg/dispose, gadgets
add/remove/hit-test/render/route-up, string gadgets, and the
key/gadget/mouse-button/close-window routers.

**New for L5:**
- `Leargas_OpenWindowTags` path: `WA_*` → `NewWindow` builder + window
  RPort sub-bitmap setup (§2.2) on `Leargas_Window_InitInPlace`.
- Window-op verbs: activate/move/size/depth/title on `LeargasWindow` +
  the active/inactive IDCMP edges.
- **Menu engine**: render + menu-button router + `IDCMP_MENUPICK` +
  `ItemAddress`.
- **Requester engine**: modal dialog builder over windows + bool gadgets.
- Intuition rendering: `DrawBorder`/`DrawImage`/`PrintIText` →
  graphics.library; `struct Border`/`Image`/`IntuiText` (IntuiText
  exists).
- Gadget-list verbs: `AddGList`/`RemoveGList`/`On`/`Off`/`RefreshGList`.

---

## 4. Surface + LVO anchors

LVO numbers come from the intuition autodoc / `intuition_lib.fd` — the KB
markdown is lost (`CLAUDE.md`); **read `amiga_docs/` to cross-check, never
copy**. Working anchors to verify at `.conf` time (canonical): `ClearMenuStrip
-54`, `CloseScreen -66`, `DoubleClick -78`, `CurrentTime -84`,
`DisplayBeep -96`, `DrawBorder -108`, `DrawImage -114`, `ItemAddress
-144`, `ModifyIDCMP -150`, `MoveWindow -168`, `OffGadget -180`, `OnGadget
-186`, `OpenScreen -198`, `PrintIText -216`, `SetMenuStrip -264`,
`SetWindowTitles -276`, `SizeWindow -288`, `WindowToBack -306`,
`WindowToFront -312`, `IntuiTextLength -330`, `AutoRequest -348`,
`ActivateWindow -450`, `RefreshGList -432`, `AddGList -438`, `RemoveGList
-444`, `RefreshWindowFrame -456`, `OpenWindowTagList -606`,
`OpenScreenTagList -612`, `EasyRequestArgs -588`. (Already implemented:
`OpenWindow -204`, `CloseWindow -72`, `AddGadget -42`, `RemoveGadget
-228`, `ActivateGadget -462`.) The generator validates
`lvo == -(bias + user_idx*6)`, so `##pad_run` counts keep declaration
order aligned. The `.conf` widens incrementally (the **exec** precedent —
declare what each slice implements, pad the long tail, coverage tracks it).

---

## 5. Slice plan (dependency-ordered, each ends green + committed)

- **L5.1 — tag window opener + window RPort.** `OpenWindowTagList` (`WA_*`
  → `NewWindow` → `Leargas_OpenWindow`); give each window a live RPort
  sub-bitmap (§2.2); `ModifyIDCMP`. New `WA_*` constants. *Done when:* a
  Gleas opens a window by tags, `RectFill`s `window->RPort`, and the
  pixels land in the window's screen region (kernel test over an
  off-screen "screen" surface).
- **L5.2 — window ops + activation.** `ActivateWindow`, `MoveWindow`,
  `SizeWindow`, `WindowToFront`/`WindowToBack`, `SetWindowTitles`; the
  `IDCMP_ACTIVEWINDOW`/`INACTIVEWINDOW` edges. *Done when:* move/activate
  a window and observe the state + IDCMP.
- **L5.3 — menus.** `struct Menu`/`MenuItem` + macros; `SetMenuStrip`/
  `ClearMenuStrip`/`ItemAddress`; Leargas menu bar render + menu-button
  router + `IDCMP_MENUPICK`. *Done when:* attach a menu, simulate a
  menu-button pick (via the input ring, like `clar_smoke`), receive
  `IDCMP_MENUPICK` with the right `Code`, and `ItemAddress` resolves it.
- **L5.4 — requesters + feedback.** `AutoRequest`/`EasyRequestArgs`
  (modal dialog), `DisplayBeep`, `CurrentTime`/`DoubleClick`/
  `ReportMouse`. *Done when:* an `AutoRequest` returns the gadget the
  simulated click hit.
- **L5.5 — rendering helpers + gadget widen.** `DrawBorder`/`DrawImage`/
  `PrintIText`/`IntuiTextLength` (+ `struct Border`/`Image`) over
  graphics.library; `AddGList`/`RemoveGList`/`OnGadget`/`OffGadget`/
  `RefreshGList`/`RefreshWindowFrame`. *Done when:* a border + text label
  render into a window RPort and the pixels check out.
- **L5.6 — screens (apps-gated).** `OpenScreenTagList`/`OpenScreen`/
  `CloseScreen` + `NewScreen` + `SA_*` over the existing
  `Leargas_OpenScreen`. *Done when:* open a custom-titled screen and a
  window on it. (Skip until an app opens its own screen.)

**L5 done (epic):** `PHASE3.md §5` — `.conf` declares the documented set
at canonical numbers; headers + a canonical V36 snippet compile/link; the
apps-driven LVOs have a `KERNEL_TEST` and/or host test; every
unimplemented LVO is a logged stub the coverage report lists.

**Testing.** Kernel tests over an **off-screen "screen" surface**
(a `DathFramebuffer` in BSS, as `graphics_screen_rastport` does) +
input-ring injection (as `clar_smoke` does) — deterministic, no display.
Tag parsing, menu `Code` packing, and requester gadget selection are unit-
testable host-side where the logic is dual-target.

---

## 6. Open questions / deferred

1. **Layers / occlusion.** §2.2's sub-bitmap gives window-relative
   clipped drawing but no occlusion. A `layers.library` phase swaps in
   per-window layers (backing store + clip lists) under the same
   `window->RPort`. Until then: non-overlapping windows only; overlap
   overdraws; `MoveWindow` may leave stale pixels.
2. **Menu depth.** v0 menus are flat text items (+ check/enable). Sub-
   menus, image items, and command-key (Amiga-key) shortcuts are
   deferred.
3. **Custom screens.** v0 screens reuse the boot framebuffer's format/
   size (one display, no mode database — that's Phase 4 RTG). `OpenScreen`
   with a different depth/mode is logged + clamped.
4. **BOOPSI / gadtools split.** The rich gadget toolkit is L7/L8, not L5.
   L5's gadget verbs operate on plain `struct Gadget`s; `gadtools` builds
   on top. Keep the seam clean so L8 doesn't have to refactor L5.
5. **Requester richness.** v0 `AutoRequest`/`EasyRequest` cover the
   common modal dialog; app-supplied custom `Requester`s in a window
   (`Request`/`EndRequest`) are declared and filled when an app needs one.
6. **Double-buffering / `WFLG_SIMPLE_REFRESH` vs `SMART_REFRESH`.**
   Moot without layers; v0 treats all windows as simple, immediate.
