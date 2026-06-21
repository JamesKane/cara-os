# L8 — gadtools.library

> Scope/design for Phase 3 epic **L8**: `gadtools.library` — the V36+
> "gadget toolkit" that builds ready-made labelled gadgets (buttons,
> checkboxes, cycles, sliders, string fields, listviews…) and menus
> without the caller hand-rolling `struct Gadget`s and imagery. Read this
> before cutting L8 code. Pairs with `docs/LEARGAS_INTUITION.md` (the L5
> window/gadget/menu substrate this sits on), `docs/LEARGAS_BOOPSI.md`
> (L7 — and §2.1 below on why gadtools does *not* depend on it),
> `docs/DATH_GRAPHICS.md`, and `docs/LVO.md`.
>
> Authoritative spec: the V36+ `<libraries/gadtools.h>`, the
> `gadtools_lib.fd`, and the RKM Libraries "GadTools" chapter (read from
> `amiga_docs/`, never copy). Verbatim names, struct field offsets, kind
> codes, attribute tags, and LVO numbers are ABI.

---

## 1. Scope

**In scope (the gadtools surface an app/Workbench-style tool needs):**

- **Library + render context** — `GetVisualInfoA` / `FreeVisualInfo`
  (a screen-derived rendering handle), `CreateContext` / `FreeGadgets`
  (the gadget-list head + teardown), `GT_SetGadgetAttrsA` /
  `GT_GetGadgetAttrsA` (post-create attribute changes).
- **The gadget factory** — `CreateGadgetA(kind, prevGad, ng, tags)`.
- **The v0 gadget kinds** — `BUTTON_KIND`, `TEXT_KIND`, `NUMBER_KIND`,
  `CHECKBOX_KIND`, `CYCLE_KIND`, `MX_KIND`, `STRING_KIND`,
  `INTEGER_KIND` (+ `GENERIC_KIND` passthrough). The bevel primitive
  `DrawBevelBoxA`.
- **IDCMP wrapping** — `GT_GetIMsg` / `GT_ReplyIMsg` / `GT_RefreshWindow`
  / `GT_BeginRefresh` / `GT_EndRefresh` (v0 mostly thin over the window
  IDCMP, doing the small amount of gadtools-internal pre/post the active
  kinds need).
- **The menu builder** — `CreateMenusA` / `LayoutMenusA` /
  `LayoutMenuItemsA` / `FreeMenus`, turning a `struct NewMenu[]` into the
  L5.3 `struct Menu`/`MenuItem` chain.
- ABI-complete headers: `<libraries/gadtools.h>` (`struct NewGadget`,
  `struct NewMenu`, the `*_KIND` codes, the `GT*_*` / `GTMN_*` /
  `BB_*` attribute tags), plus the `struct DrawInfo` /
  `struct VisualInfo` shapes (DrawInfo currently only exists as
  per-screen `DetailPen`/`BlockPen`).

**Out of scope (deferred / later):**

- **Prop-based kinds** — `SLIDER_KIND`, `SCROLLER_KIND`, `LISTVIEW_KIND`,
  `PALETTE_KIND`. These need **drag-tracking** (track the pointer while
  the button is held → update a prop pot → emit `IDCMP_MOUSEMOVE` /
  `GADGETUP`), which the Leargas input router does **not** do yet
  (`router.c`: SELECTUP/drag is explicitly deferred). That new substrate
  capability + the prop kinds are **L8.5**, after the static kinds land —
  or deferred past a v0 entirely. Tracked, declared ABI-complete (stubs).
- **BOOPSI gadget classes** (`gadgetclass`/`propgclass`/`strgclass`/
  `buttongclass`) — gadtools v0 does **not** use them (§2.1). They are a
  separate, optional later effort for apps that drive BOOPSI directly.
- **`GT_FilterIMsg`/`GT_PostFilterIMsg`** (the raw filter hooks) — v0
  folds the needed filtering into `GT_GetIMsg`; declared, stubbed.
- **TextAttr/font selection per gadget** — v0 uses the one Dath 8×8 face
  (as L4.5/L5); `ng_TextAttr` is read but a non-default face is ignored.

**Done-bar for L8:** a V36 Gleas `GetVisualInfoA`s a screen,
`CreateContext`s a glist, `CreateGadgetA`s a BUTTON + CHECKBOX + CYCLE +
STRING gadget onto a window, the gadgets render correctly, an injected
click drives `GT_GetIMsg` → an `IDCMP_GADGETUP`/checkbox-toggle/cycle-
advance the program observes, `GT_SetGadgetAttrsA` changes a gadget, a
`NewMenu[]` → `CreateMenusA`/`LayoutMenusA` builds a working menu strip,
and `FreeGadgets`/`FreeMenus`/`FreeVisualInfo` tear down — plus
ABI-complete declaration + stub coverage for the prop kinds.

---

## 2. The key decisions

### 2.1 gadtools-over-Leargas, **not** over-BOOPSI

Classic AmigaOS V36/V37 gadtools builds **plain `struct Gadget`s** with a
gadtools-private `SpecialInfo`, rendered by gadtools/intuition. It does
**not** use BOOPSI internally — `gadgetclass` is a *parallel* mechanism
that arrived alongside it.

Decision: **`CreateGadgetA` builds a plain `struct Gadget` and chains it
onto the existing Leargas gadget substrate** (`src/croi/leargas/gadget.c`,
`string_gadget.c`), extending the per-kind rendering and behaviour. This:

- reuses everything already shipped (AddGadget/RemoveGadget/HitTest/
  Render/focus/`RouteUp`→`IDCMP_GADGETUP`, the string field, the L5.5
  `AddGList`/`OnGadget`/… LVOs),
- matches how real gadtools works (no BOOPSI dependency), and
- **decouples L8 from building concrete BOOPSI gadget classes** — those
  become a separate later effort, used by apps directly, not a gadtools
  prerequisite.

L7's BOOPSI machinery is therefore *available to apps* but unused by
gadtools v0. (`GadgetInfo` is defined for ABI but largely inert here.)

### 2.2 The library: a new `src/croi/gadtools`, all `syscall`

`gadtools.library` is base-ful (`GadToolsBase`), constructed at boot via
`Croi_MakeLibrary` like intuition/graphics/utility. Mirror the
**intuition_lib pattern** (kernel-side bridge separate from the
dual-target Leargas substrate):

- **New riscv64-only dir `src/croi/gadtools`** = the kernel-side
  `gadtools.library`: `Croi_GT_*_Impl` bodies (bridging onto `Leargas_*`
  + `Croi_Gfx_*_Impl`), the generated `gadtools_vec.c`, `trampolines.S`,
  and the reserved-hook TU. Whole-archived into croi (so trampolines +
  vec survive GC), `KEEP(.lib_text.gadtools)` in `croi.lds`.
- `tools/lvo-gen/gadtools.conf` (`##library gadtools.library`,
  `##base GadToolsBase`, `##owner croi/gadtools`). Added to
  `lvo-coverage` + an `add_dependencies(<app> cara_gadtools_lib_gen)`.
- Construction block in `entry.c` (shared-heap base + vec).

No Irish brand is assigned — the dir/impls are named after the library
(`gadtools` / `Croi_GT_*`), exactly as `intuition_lib` names the kernel
side of Leargas. The shipped binary is `gadtools.library` (the API
namespace stays verbatim).

**Flavour:** every implemented LVO is ≤7 args, so **all `syscall`** — no
marshalling stubs. The kernel allocates the gadget / SpecialInfo /
VisualInfo / Menu structs in the **SASOS shared heap** (`Croi_AllocShared`,
U-mode-visible), fills them, and chains via the Leargas substrate, exactly
like the intuition window opener.

### 2.3 `VisualInfo` + `DrawInfo` (v0 shape)

`GetVisualInfoA(screen, tags)` returns an opaque `APTR VisualInfo`. v0:
a small shared-heap struct carrying the `struct Screen *` and a pointer to
the screen's `struct DrawInfo`. `FreeVisualInfo` frees it.

`struct DrawInfo` is not yet defined (the screen only carries
`DetailPen`/`BlockPen`). Define it verbatim (`<intuition/screens.h>`):
`dri_Version`, `dri_NumPens`, `dri_Pens` (a `UWORD *` pen array —
`DETAILPEN`/`BLOCKPEN`/`TEXTPEN`/`SHINEPEN`/`SHADOWPEN`/`FILLPEN`/…),
`dri_Font`, `dri_Depth`, `dri_Resolution`, `dri_Flags`. v0 builds one
DrawInfo per screen from a fixed default pen map over the L4.2 8-entry
palette; the matching intuition `GetScreenDrawInfo` LVO can land here too
(it sits in the intuition `-690..-702` pad reserved in L7.3).

### 2.4 `CreateGadgetA` / context / kinds

```c
struct NewGadget {
    WORD  ng_LeftEdge, ng_TopEdge, ng_Width, ng_Height;
    STRPTR ng_GadgetText;
    struct TextAttr *ng_TextAttr;   // v0: ignored (one face)
    UWORD ng_GadgetID;
    ULONG ng_Flags;                 // PLACETEXT_* etc.
    APTR  ng_VisualInfo;
    APTR  ng_UserData;
};
```

- `CreateContext(struct Gadget **glistptr)` allocates a placeholder
  "context" gadget (`GTYP_GADGET0002`-tagged, no imagery), stores it in
  `*glistptr`, returns it. It is the list head + the `prevGad` seed.
- `CreateGadgetA(kind, prevGad, ng, tags)` allocates a shared-heap
  `struct Gadget` (+ kind SpecialInfo), fills geometry from `ng`,
  `GadgetID`, label from `ng_GadgetText`, the `GTYP_*`/`GFLG_*` for the
  kind, the kind's initial state from the `GT*_*` tags, links it after
  `prevGad`, and returns it (nullptr → frees the whole list, the V36
  error contract). The app then `AddGList`s the chain to its window (or
  opens the window with `FirstGadget` = the first real gadget).
- `FreeGadgets(glist)` walks the chain freeing gadgets + SpecialInfo.

**v0 kinds** (rendered/behaved via the Leargas substrate):

| Kind | Backing | Behaviour |
|---|---|---|
| `BUTTON_KIND` | bool gadget | click → `IDCMP_GADGETUP` (have it) |
| `CHECKBOX_KIND` | bool + GFLG_SELECTED | click toggles, `GTCB_Checked` |
| `TEXT_KIND` | display-only | renders `GTTX_Text` in a bevel box |
| `NUMBER_KIND` | display-only | renders `GTNM_Number` |
| `CYCLE_KIND` | bool | click advances `GTCY_Labels` index → GADGETUP, `GTCY_Active` |
| `MX_KIND` | bool group | radio over `GTMX_Labels`, `GTMX_Active`, MutualExclude |
| `STRING_KIND` | string gadget | reuse `string_gadget.c`, `GTST_String`/`GTST_MaxChars` |
| `INTEGER_KIND` | string gadget | string field + integer parse, `GTIN_Number` |

`DrawBevelBoxA(rp, l, t, w, h, tags)` renders a recessed/raised bevel
(two L-shaped border lines in SHINE/SHADOW pens) over a RastPort — the
visual primitive the kinds and `TEXT`/`NUMBER` boxes use.

### 2.5 `GT_GetIMsg` / `GT_ReplyIMsg` + the active kinds

`GT_GetIMsg(port)` wraps the window `GetMsg`: it pulls the next
`IntuiMessage`, performs the gadtools-internal update for the gadget the
message refers to (advance a CYCLE's active index, toggle a CHECKBOX,
flip MX selection, commit a STRING), and returns the (possibly rewritten)
message; the app reads the new state via `GT_GetGadgetAttrsA` or the
gadget's SpecialInfo. `GT_ReplyIMsg` replies it. v0 is otherwise a thin
pass-through over the existing IDCMP path; `GT_RefreshWindow`/
`GT_BeginRefresh`/`GT_EndRefresh` re-render the gadtools gadgets
(`Leargas_Window_RenderGadgets`).

### 2.6 The menu builder

```c
struct NewMenu {
    UBYTE  nm_Type;        // NM_TITLE / NM_ITEM / NM_SUB / NM_END / IM_*
    STRPTR nm_Label;       // text, or NM_BARLABEL for a separator
    STRPTR nm_CommKey;
    UWORD  nm_Flags;
    LONG   nm_MutualExclude;
    APTR   nm_UserData;
};
```

`CreateMenusA(newmenu[], tags)` walks the flat `NewMenu[]`
(NM_TITLE→Menu, NM_ITEM→MenuItem, NM_SUB→subitem, NM_END terminates) and
allocates the L5.3 `struct Menu`/`MenuItem` chain (shared heap) with
`IntuiText` labels; `LayoutMenusA(menu, vi, tags)` assigns the bar/drop
geometry (calls the existing `Leargas_Menu_Layout`); `LayoutMenuItemsA`
lays a single menu; `FreeMenus(menu)` tears down. The app then
`SetMenuStrip`s the result (L5.3). v0 inherits L5.3's simplifications
(flat text items, all-dropdowns, no command-key glyphs, no imagery).

---

## 3. LVO offsets (canonical — **verify at L8.1**)

From `gadtools_lib.fd` (reserved slots 0..3 = -6..-24; confirm each
against `amiga_docs/` before declaring — offsets are ABI and feed Phase
9). All implemented rows are `syscall`.

| LVO | offset | ord | slice |
|-----|--------|-----|-------|
| `CreateGadgetA`     | -30  | 4  | L8.2 |
| `FreeGadgets`       | -36  | 5  | L8.2 |
| `GT_SetGadgetAttrsA`| -42  | 6  | L8.2 |
| `CreateMenusA`      | -48  | 7  | L8.4 |
| `FreeMenus`         | -54  | 8  | L8.4 |
| `LayoutMenuItemsA`  | -60  | 9  | L8.4 |
| `LayoutMenusA`      | -66  | 10 | L8.4 |
| `GT_GetIMsg`        | -72  | 11 | L8.4 |
| `GT_ReplyIMsg`      | -78  | 12 | L8.4 |
| `GT_RefreshWindow`  | -84  | 13 | L8.4 |
| `GT_BeginRefresh`   | -90  | 14 | L8.4 |
| `GT_EndRefresh`     | -96  | 15 | L8.4 |
| `GT_FilterIMsg`     | -102 | 16 | stub |
| `GT_PostFilterIMsg` | -108 | 17 | stub |
| `CreateContext`     | -114 | 18 | L8.1 |
| `DrawBevelBoxA`     | -120 | 19 | L8.3 |
| `GetVisualInfoA`    | -126 | 20 | L8.1 |
| `FreeVisualInfo`    | -132 | 21 | L8.1 |
| `GT_GetGadgetAttrsA`| -138 | 22 | L8.3 (V39) |

`##pad_run` fills the gaps as usual; each implemented row splits its pad
at its ordinal.

---

## 4. Slice plan

- **L8.1 — library + render context.** `gadtools.conf` + `GadToolsBase` +
  boot construction; `<libraries/gadtools.h>` (NewGadget/NewMenu/kinds/
  tags) + `struct DrawInfo`/`VisualInfo`. `GetVisualInfoA`/`FreeVisualInfo`
  (over a default per-screen DrawInfo), `CreateContext`/`FreeGadgets`.
  **Test:** GetVisualInfo + CreateContext + FreeGadgets + FreeVisualInfo
  round-trip from a Gleas.
- **L8.2 — the gadget factory + easy kinds.** `CreateGadgetA` +
  BUTTON/TEXT/NUMBER/CHECKBOX + `GT_SetGadgetAttrsA` + per-kind Leargas
  rendering. **Test:** create the four, AddGList to a window, verify
  geometry/labels + a checkbox toggle.
- **L8.3 — choice + edit kinds + bevel.** `DrawBevelBoxA`; CYCLE
  (click-advance + `GTCY_Active`), MX (radio), STRING/INTEGER (reuse
  `string_gadget`) + `GT_GetGadgetAttrsA`. **Test:** cycle advance, MX
  select, string commit via injected input.
- **L8.4 — IDCMP wrap + menu builder.** `GT_GetIMsg`/`GT_ReplyIMsg`/
  `GT_RefreshWindow`/`GT_Begin`/`GT_EndRefresh`; `CreateMenusA`/
  `LayoutMenusA`/`LayoutMenuItemsA`/`FreeMenus` over L5.3. **Test:**
  GT_GetIMsg delivers a rewritten gadget message; a NewMenu[] builds a
  strip that SetMenuStrip + a menu pick resolves.
- **L8.5 — prop gadgets (or defer).** Drag-tracking in `router.c`
  (button-held mouse-move → prop pot → `IDCMP_MOUSEMOVE`/`GADGETUP`),
  then SLIDER/SCROLLER/LISTVIEW/PALETTE. The hardest substrate work;
  may ship as its own epic-tail or be deferred past the L8 done-bar.

---

## 5. Testing

gadtools LVOs are `syscall` (kernel-side), so unlike BOOPSI's local
dispatch they **can** be exercised both from a Gleas and, for the
non-input parts, a `KERNEL_TEST` against `Croi_GT_*_Impl` over an
off-screen Leargas screen (the `graphics_screen_rastport` /
`intuition_*` pattern). Interactive behaviour (clicks, string entry,
menu picks) is driven by **input-ring injection** (the `clar_smoke` /
requester pattern: pre-post the event, then `GT_GetIMsg`). A Gleas
(extend `userintuition.c` or a new `usergadtools.c`) covers the end-to-end
`OpenLibrary("gadtools.library")` → CreateGadget → render → message path.

---

## 6. Tracked gaps / deferrals

- Prop kinds (SLIDER/SCROLLER/LISTVIEW/PALETTE) + router drag-tracking
  (L8.5 or deferred).
- BOOPSI gadget classes (gadgetclass/propgclass/strgclass/buttongclass)
  — separate later effort; gadtools v0 doesn't need them.
- `GT_FilterIMsg`/`GT_PostFilterIMsg` raw hooks — folded into GT_GetIMsg.
- Per-gadget fonts/`TextAttr`, command-key menu glyphs, menu imagery,
  submenus (inherits the L5.3 limits).
- `GetScreenDrawInfo`/`FreeScreenDrawInfo` (intuition) — land alongside
  the DrawInfo definition (the L7.3 intuition `-690..-702` pad).
- BOOPSI image-backed gadget imagery (`DrawImageState`) — gated on the
  deferred L5 `DrawImage`.
