# Phase 1 Subgoal 6 — Leargas (Intuition)

> Plan for the Phase 1 minimum window-system substrate. Pairs with
> `docs/ROADMAP.md` Phase 1 Subgoal 6 (the one-line requirement),
> `docs/PHASE1_RUNTIME.md` (Croi runtime substrate),
> `docs/PHASE1_FRAMEBUFFER.md` (Dath rasteriser this draws into),
> `docs/PHASE1_USB.md` (input event source), and
> `docs/ARCHITECTURE.md` §11 (the brand-vs-API split — `Leargas`
> is the brand-namespace implementation Phase 3's
> `intuition.library` trampolines into).

---

## Status — 2026-05-08

**Not started.** The substrate Leargas needs is in place: Dath
rasteriser, kernel-internal IPC (MsgPort), per-task signals, and
the lvo-gen pipeline that will eventually publish
`intuition.library`'s LVOs. The input pipeline (USB, Subgoal 5)
is also a hard prerequisite — no input means no useful Intuition.
This doc plans Leargas; implementation starts after PHASE1_USB.md
Tier 1 (xHCI controller online) at minimum, with Tier 3 (HID
Gleas) needed before keyboard events flow.

---

## Context

`docs/ROADMAP.md` Phase 1 Subgoal 6 says:

> **Leargas (Intuition).** Mouse pointer rendered, keyboard events
> delivered to the focused window.

Phase 3's `intuition.library` is the full V36+ surface — screens,
windows, menus, requesters, gadgets, BOOPSI, IDCMP, the lot.
That's a years-long implementation if taken whole. **Leargas in
Phase 1 is the strict minimum substrate Clar (Subgoal 7) needs
to function**, plus the canonical V36+ object shapes so a Phase 3
program written against `<intuition/intuition.h>` compiles and
links cleanly without source edits when Leargas grows under it.

**The "Leargas in Phase 1 vs intuition.library in Phase 3"
relationship.** Same brand (Leargas), same internal struct
namespace (`Leargas_*`), same kernel objects. Phase 1 ships the
core implementations Clar drives directly; Phase 3 wraps those in
the V36+ LVO surface (`OpenScreen`, `OpenWindow`, `IDCMP`,
`AddGadget`, …) via the lvo-gen pipeline using a hand-written
`intuition.conf`. No re-implementation seam — Phase 3 adds the
exposed LVO names, not new logic.

### Strategy

**The simplest thing that draws Clar.** What Clar needs:
1. A pointer (mouse cursor) that follows mouse motion.
2. A "screen" — a single rectangular region the size of the
   framebuffer, painted dark blue, whose contents Clar owns.
3. At least one "window" — a rectangle inside the screen with a
   title bar that Clar can move/resize/close.
4. Focus tracking — exactly one window is focused at a time.
5. A keyboard event delivery path — keypresses go to the focused
   window's IDCMP message queue.
6. A "gadget" abstraction Clar can use for the text Inntin
   (input gadget) the success criterion calls out.

That's six concrete things. Everything past that — multiple
screens, requesters, menus, refresh modes other than SMART_REFRESH,
custom screens, Workbench-screen-vs-application-screen distinction,
proportional gadgets, etc. — is Phase 3 / future scope.

### Out of scope for Phase 1's Leargas

- **Multiple screens.** Phase 1 has one screen (`Workbench` /
  `Clar`); custom application screens arrive in Phase 3.
- **Menus / requesters.** Phase 3.
- **BOOPSI.** Phase 3 (the class system is its own deep cut).
- **`gadtools.library`.** Phase 3.
- **Sprites beyond the pointer.** Phase 4 / animation.
- **Layers (sub-screen clipping for overlapping windows).** Phase
  1 handles overlap by simple repaint; Phase 3+4 add proper
  layer tracking.
- **Double-buffered windows.** Phase 4.
- **IDCMP message types past `MOUSEMOVE`, `MOUSEBUTTONS`,
  `RAWKEY`, `CLOSEWINDOW`, `ACTIVEWINDOW`, `INACTIVEWINDOW`,
  `GADGETUP`.** The rest land as the V36+ surface fills out in
  Phase 3.

---

## Tier 1 — Pointer + screen

**Exit:** the framebuffer shows a CaraOS-themed pointer that
tracks mouse motion received from the input ring; behind the
pointer is a single-colour screen. No windows yet.

### Epic LA — Pointer rendering

**Goal:** a 16×16 (or 32×32) sprite-shaped pointer is composited
on top of the framebuffer. As the mouse moves, the pointer's
previous location is restored (XOR-style or save/restore-buffer
based) and the new location is drawn.

- **LA.1** `struct LeargasPointer` carries pointer image
  (`DathBitmap *`), hot-spot offset, current `(x, y)`, and a
  save-buffer of the framebuffer pixels currently under the
  pointer.
- **LA.2** `Leargas_Pointer_Move(x, y)`: blits save-buffer back
  to framebuffer at old position, captures save-buffer at new
  position, draws pointer image at new position.
- **LA.3** A default arrow pointer image — 16×16, same generation
  pattern as the 8×8 font (a `tools/font-gen`-style helper or
  hand-coded glyph). Hot-spot at `(0, 0)`.

### Epic LB — Screen

**Goal:** the V36+ `struct Screen` shape (from
`<intuition/screens.h>`) is allocated, populated with a Dath
framebuffer reference, and rendered (filled with `Pen 0` in
Phase 1, configurable later via Clar).

- **LB.1** `<intuition/screens.h>` defines `struct Screen` with
  the V36+ public field set (`NextScreen`, `FirstWindow`,
  `LeftEdge`/`TopEdge`/`Width`/`Height`,
  `MouseY`/`MouseX`, `Flags`, `Title`, `DefaultTitle`,
  `BarHeight`, `BarVBorder`, `BarHBorder`, `MenuVBorder`,
  `MenuHBorder`, `WBorTop`/`WBorLeft`/`WBorRight`/`WBorBottom`,
  `Font`, `ViewPort`, `RastPort`, `BitMap`, `LayerInfo`,
  `FirstGadget`, `DetailPen`/`BlockPen`, `SaveColor0`,
  `BarLayer`, `ExtData`, `UserData`).
- **LB.2** `Leargas_OpenScreen(struct Screen *)` — kernel-side
  impl that allocates the V36+ struct, points its `RastPort` /
  `BitMap` / `LayerInfo` at a Dath surface, paints the
  background.
- **LB.3** Tracks the active screen on a global; Phase 1 has
  exactly one. Phase 3 evolves to a list.

### Epic LC — Mouse motion → pointer

**Goal:** events from the input ring (PHASE1_USB Tier 3 HID Gleas)
update the pointer's `(x, y)` and clamp to the screen extent.

- **LC.1** A Leargas Gleas (U-mode task) opens the input ring,
  blocks on it, processes `IECLASS_RAWMOUSE` events into pointer
  motion calls.
- **LC.2** Per-event clamp to screen dimensions; report
  pointer-out-of-screen as a logical no-op.
- **LC.3** Mouse-button events update Leargas's button state and
  generate IDCMP events for whichever window the pointer is
  over (Tier 2).

---

## Tier 2 — Windows + focus

**Exit:** a hand-crafted test program (or Clar in stub form)
opens two windows; clicking inside one transfers focus; keyboard
events go only to the focused window's IDCMP port.

### Epic LD — Window primitives

**Goal:** the V36+ `struct Window` shape (from
`<intuition/intuition.h>`) is allocated, drawn with a title bar,
border, and close gadget, and tracked in the screen's window list.

- **LD.1** `<intuition/intuition.h>` defines `struct Window` with
  the V36+ public field set (`NextWindow`, `LeftEdge`/`TopEdge`/
  `Width`/`Height`, `MouseY`/`MouseX`, `MinWidth`/`MinHeight`/
  `MaxWidth`/`MaxHeight`, `Flags`, `MenuStrip`, `Title`,
  `FirstRequest`, `DMRequest`, `ReqCount`, `WScreen`, `RPort`,
  `BorderLeft`/`BorderTop`/`BorderRight`/`BorderBottom`,
  `BorderRPort`, `FirstGadget`, `Parent`, `Descendant`, …,
  `IDCMPFlags`, `WindowPort`, `MessageKey`, `DetailPen`/`BlockPen`,
  `CheckMark`, `Screen`, `BitMap`, `MinWidth`, `RequestPort`,
  `UserPort`, `ExtData`, `UserData`).
- **LD.2** `Leargas_OpenWindow(struct NewWindow *)` — V36+
  semantics. Allocates the window, draws its decorations onto
  the screen's RastPort using Dath, links into the screen's
  window list.
- **LD.3** `Leargas_CloseWindow(struct Window *)` — unlinks,
  redraws the screen region the window occupied (Phase 1 just
  blanks; Phase 3+4 redraws the underneath via Layers).
- **LD.4** Title bar drawing — uses Dath text primitives, draws
  the title text + a close gadget icon on the right.

### Epic LE — Focus + activation

**Goal:** mouse-click events change which window is focused; the
focused window is highlighted; keyboard events route there.

- **LE.1** `Leargas_ActiveWindow()` returns the focused window;
  state is a single global pointer.
- **LE.2** Mouse-button down events: hit-test against the screen's
  window list (front-to-back), set focus to the topmost hit
  window, generate `IDCMP_ACTIVEWINDOW` for the new focus and
  `IDCMP_INACTIVEWINDOW` for the old.
- **LE.3** Title-bar redraw on focus change (filled vs. outline,
  the V36+ visual distinction).

### Epic LF — Keyboard event routing

**Goal:** `IECLASS_RAWKEY` events from the input ring become
`IDCMP_RAWKEY` IntuiMessages on the focused window's IDCMP port.

- **LF.1** `<exec/ports.h>`-shaped IDCMP port per window
  (`window->UserPort`). Phase 1 uses a `KOBJ_MSGPORT`
  per-window; Phase 3 wraps via `OpenWindow`'s `IDCMP_*` flags.
- **LF.2** `struct IntuiMessage` (V36+ shape) carrying the event
  class, code, qualifier, mouse coords, and a back-pointer to
  the window.
- **LF.3** Keyboard delivery: `IECLASS_RAWKEY` → translate to
  IntuiMessage with `Class = IDCMP_RAWKEY`, `Code = rawkey
  code`, post to the focused window's UserPort.
- **LF.4** A test program registers an IDCMP port, blocks on it
  with `WaitPort`, asserts a sequence of pressed keys arrives in
  order with the right qualifier bits.

---

## Tier 3 — Gadgets (just the text Inntin)

**Exit:** a string Inntin (input gadget) inside a window accepts
typed characters, displays them with a blinking cursor, and posts
`IDCMP_GADGETUP` when Enter is pressed.

### Epic LG — Gadget framework

**Goal:** the V36+ `struct Gadget` shape (from
`<intuition/intuition.h>`) is allocated, attached to a window via
the `FirstGadget` chain, drawn, and hit-tested on mouse clicks.

- **LG.1** `struct Gadget` V36+ field set (`NextGadget`,
  `LeftEdge`/`TopEdge`/`Width`/`Height`, `Flags`/`Activation`/
  `GadgetType`, `GadgetRender`, `SelectRender`, `GadgetText`,
  `MutualExclude`, `SpecialInfo`, `GadgetID`, `UserData`).
- **LG.2** Hit-testing: on mouse-down inside a window, walk the
  gadget chain, find the topmost hit, call its activation
  handler.
- **LG.3** Gadget render path: gadgets draw themselves into the
  window's RastPort using Dath primitives; refresh is on
  activation / state change.

### Epic LH — String Inntin

**Goal:** the canonical V36+ string gadget — single-line text
input, fixed-width, blinking cursor, posts `IDCMP_GADGETUP` on
Enter.

- **LH.1** `struct StringInfo` (V36+ shape) carries the buffer
  pointer, max length, current length, cursor position, undo
  buffer.
- **LH.2** Render: Dath `DrawString` of the buffer into the
  gadget rect, plus a vertical-line cursor at the
  current position.
- **LH.3** Keyboard handling when the gadget is active: ASCII
  characters append to the buffer; backspace deletes; Enter
  generates `IDCMP_GADGETUP` with the gadget's `GadgetID`.

---

## What this unblocks

- **Phase 1 Subgoal 7 (Clar).** Workbench / Clar is just a
  Leargas client — it opens a screen, opens windows for each
  drawer, places a string Inntin in each. Everything Clar
  draws goes through the surface above.
- **Phase 3 `intuition.library`.** The lvo-gen `.conf` for
  intuition.library declares its V36+ LVOs (`OpenScreen`,
  `OpenWindow`, `WaitPort`, `GetMsg`, `ReplyMsg`,
  `ActivateWindow`, `MoveWindow`, `SizeWindow`,
  `SetWindowTitles`, `RefreshWindowFrame`, `AddGadget`,
  `RemoveGadget`, `ActivateGadget`, `RefreshGList`,
  `OffGadget`/`OnGadget`, `ModifyIDCMP`, …) as `local`-flavour
  against the `Leargas_*` symbols Phase 1 ships, plus
  `syscall`-flavour for anything that needs kernel state (the
  global active-window pointer, the input ring). The Phase 1
  cut already provides the impls; Phase 3 just publishes them
  under the V36+ names.
