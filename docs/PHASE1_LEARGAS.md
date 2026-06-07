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

## Status — 2026-06-06

**Tier 1 done (L0+LA+LB+LC); LD+LE+LF shipped. LG/LH remain.**
PHASE1_USB.md Tier 1–2 + HA.1–4 + HidIntReadOnce are in HEAD;
that's enough to feed the L0 producer side. The L0 commit
lands:

- `<devices/inputevent.h>` — V36+ canonical `struct InputEvent`
  shape + IECLASS_* / IECODE_* / IEQUALIFIER_* / IESUBCLASS_*
  constants. Source-compatible with V36+ programs.
- `<devices/timer.h>` — V36+ `struct timeval` (embedded in
  `struct InputEvent`).
- `<cara/leargas.h>` — brand-namespace flat `struct
  LeargasInputEvent` (24 bytes) + `Leargas_Input_Init / _Post /
  _Read / _Pending / _Reset` SPSC contract.
- `src/croi/leargas/input_ring.c` — fixed-capacity (64) static
  SPSC ring, mirrors the atomic ordering of `cara/ring.h`.
- `src/croi/entry.c` — wires the kernel HID poll: decoded
  reports translate into LeargasInputEvent and Post; drain-log
  at end of poll confirms the seam end-to-end.
- `tests/unit/test_leargas_input.c` — round-trip, FIFO,
  backpressure, wrap-around, NULL guards (test 11/13).

End-to-end smoke (`sendkey a` after kbd poll opens) shows the
seam: `larg: L0 ring drained 1 event(s)` with `class=0x1
code=0x20 qual=0x0` (rawkey for 'A').

The LA commit adds:

- `struct LeargasPointerImage` (ternary mask: transparent / fg /
  bg pixels) + the default 16×16 arrow `leargas_pointer_arrow`
  with hot-spot at the tip (0, 0).
- `struct LeargasPointer` + `Leargas_Pointer_Init` /
  `Leargas_Pointer_Move`. Save-and-restore via Dath_BlitRect
  against a caller-provided same-format save buffer (kernel:
  Dath_AllocBitmap; tests: stack-mounted synthetic fb).
  Composite via per-pixel Dath_Pixel walking the mask.
- `tests/unit/test_leargas_pointer.c` — Init invariants,
  paint-then-restore exact-pixel match, no-op move, multi-trip
  stability, off-screen clip without corruption, default-arrow
  layout sanity.

LA is intentionally not yet wired into entry.c at boot — that
integration belongs to LC (mouse-motion-to-pointer), which is the
first epic that has both the framebuffer and a reason to drive
the pointer. Currently entry.c just drains the L0 ring at the
end of HID poll for the seam log line.

The LB commit adds:

- `<intuition/screens.h>` — V36+ canonical `struct Screen` with
  the public field set (NextScreen, FirstWindow, LeftEdge…
  LayerInfo… UserData). **Pragmatic Phase 1 deviation:** V36+
  embeds ViewPort / RastPort / BitMap / Layer_Info BY VALUE;
  Phase 1 carries forward-declared POINTERS instead. Field
  NAMES match V36+; the layout shape changes when graphics.library
  ships in Phase 3 / Phase 4.
- `<graphics/{gfx,view,rastport,layers,text}.h>` and
  `<intuition/intuition.h>` — minimal forward-decl headers so
  the canonical V36+ include paths resolve. Full type bodies
  arrive epic-by-epic.
- `struct LeargasScreen` (cara/leargas.h) — brand wrapper with
  `struct Screen pub` first; CroiMsgPort-style cast / container_of.
- `Leargas_Screen_InitInPlace / SetActive / ActiveScreen / FromPub`
  in src/croi/leargas/screen.c (dual-target, host-testable).
- `Leargas_OpenScreen / CloseScreen` in screen_alloc.c (kernel
  only — heap-backed via Croi_Alloc; sets active, paints
  background with Pen 0 via Dath_Clear).
- `tests/unit/test_leargas_screen.c` (test 13/15) — InitInPlace
  invariants, title bounded copy + truncation, active-screen
  set/clear/round-trip, FromPub round-trip, layout invariant
  (pub at offset 0).

LB does not yet wire entry.c either — boot-time OpenScreen is
deferred until LC lands a mouse consumer that needs an active
screen as render target. Once both are in, entry.c flips from
draining the L0 ring with a log line to OpenScreen + Pointer_Init
+ a per-event mouse-motion handler.

The LC commit adds:

- `Leargas_Input_Drain(struct LeargasPointer *p)` in
  src/croi/leargas/router.c. Reads the L0 ring, accumulates
  IECLASS_RAWMOUSE deltas onto the pointer, clamps against the
  active screen extent, calls Pointer_Move; mirrors the
  position back into Screen.MouseX/Y for hit-testers
  (LE-onwards). RAWKEY events are consumed but dropped here —
  LF will pick them up when keyboard routing lands.
- entry.c boot wiring: at framebuffer-up, Dath_AllocBitmap a
  pointer save buffer + Leargas_OpenScreen("Workbench", bg) +
  Leargas_Pointer_Init at center-screen. After the HID poll,
  call Leargas_Input_Drain(&g_pointer) and log the resulting
  pointer position. Headless boots (no FDT simple-framebuffer)
  skip this entirely — the L0 producer still posts events but
  nothing drains them.
- `tests/unit/test_leargas_router.c` (test 14/16) — empty-ring
  no-op, NULL-safety, no-screen accumulation, screen clamp at
  both edges, MouseX/Y mirror, RAWKEY skip-but-count, ordered
  multi-event accumulation across the clamp boundary.

End of Tier 1: pointer + screen substrate is now end-to-end
wired — under a real framebuffer (real silicon or
QEMU `-device ramfb` + a DTS overlay) Boot ends with a visible
pointer at center-screen, and any mouse delta from the HID
poll moves it inside the screen extent.

The LD commit adds:

- `<intuition/intuition.h>` — V36+ struct Window + struct
  NewWindow + WFLG_* / IDCMP_* constants. Same pragmatic
  pointer-where-V36+-embeds deviation as struct Screen.
- `struct LeargasWindow` (cara/leargas.h) — brand wrapper with
  `struct Window pub` first; CroiMsgPort-style FromPub.
- `Leargas_Window_InitInPlace / Render / FromPub /
  LinkToScreen / UnlinkFromScreen` (dual-target, host-testable).
- `Leargas_OpenWindow / CloseWindow` (kernel-only, heap-backed).
- entry.c boot wiring opens a sample "Croi" window centered on
  the screen with WFLG_DRAGBAR | DEPTHGADGET | CLOSEGADGET |
  ACTIVATE before Pointer_Init so the pointer save buffer
  captures the with-window pixels at center. Pointer outline
  passes black (not the screen pen0) so the arrow's edge is
  visible against any backdrop.
- The framebuffer LogSink is no longer registered: once Leargas
  takes over the screen, log lines would overpaint windows on
  every LOG_INFO call. UART log keeps everything for debugging;
  Phase 3+ can route logs to a dedicated console window via the
  same DathConsole emit fn (kept around in g_fb_console).
- `tests/unit/test_leargas_window.c` (test 15/17) — argument
  validation, active-screen fallback, field seeding,
  borderless flag, FromPub round-trip, link/unlink across
  multi-window screens, decoration rendering bounds (pixels
  inside / outside window rect).
- Visual verification under QEMU virt with a custom DTB
  injecting a `simple-framebuffer` node at 0x9F000000 (800×480
  RGBA8888) confirmed end-to-end rendering: dark-blue screen
  background, lighter-blue window chrome, white "Croi" title
  text, close gadget on the right, mid-blue window body, and
  the 16×16 black-outlined-white-fill arrow pointer at center.
  Captured via QEMU monitor `pmemsave` of the framebuffer
  region into a PPM.

The LE commit adds:

- `Leargas_ActiveWindow` / `Leargas_SetActiveWindow` /
  `Leargas_Window_HitTest` (+ test-only `Leargas_Focus_Reset`) in
  src/croi/leargas/focus.c (dual-target). A single process-wide
  active-window pointer; SetActiveWindow flips `WFLG_WINDOWACTIVE`
  on the outgoing/incoming window and redraws both title bars.
  HitTest walks the screen's `FirstWindow` list front-to-back
  (right/bottom edges exclusive) and returns the topmost hit.
- `Leargas_Pointer_Hide` / `Leargas_Pointer_Show` (pointer.c) so
  chrome under the cursor can be repainted without the stale save
  buffer clobbering it; the router brackets focus-driven redraws
  with them.
- router.c (LC → LE): a left-button-down (IECODE_LBUTTON, the
  down-stroke — up carries IECODE_UP_PREFIX) hit-tests at the
  pointer hot-spot against the active screen and re-focuses the
  hit window. Clicking empty screen space leaves focus unchanged;
  Phase 1 does not raise-to-front (no depth-arrange).
- window.c (LE.3): `Leargas_Window_Render` reads `WFLG_WINDOWACTIVE`
  and paints the active window's title bar / frame in a brighter
  blue (`0x3878D0`) vs. the inactive `0x204080` — the Phase 1
  stand-in for V36+'s active-vs-inactive title-bar fill. A window
  opened with `WFLG_ACTIVATE` is focused by `Leargas_OpenWindow`.
- entry.c logs `active window: <title>` after the boot OpenWindow
  (WFLG_ACTIVATE), and a real left-click through the HID poll →
  `Leargas_Input_Drain` re-focuses under the pointer.
- `tests/unit/test_leargas_focus.c` (test 16/17) — hit-test
  geometry (overlap → front-most, exclusive edges, off-window
  null), focus flag transitions (activate / switch / idempotent
  re-activate / clear), active-vs-inactive chrome pixels, and the
  router button-to-focus path (click A, move + click B, click
  empty → unchanged).

**IDCMP_ACTIVEWINDOW/INACTIVEWINDOW delivery stays with LG+.** LE
owns the focus state, the flag, and the redraw; LF added the
per-window IDCMP port + `struct IntuiMessage` that those focus
messages will ride. Wiring `Leargas_SetActiveWindow` to post them is
deferred until a focus-event consumer exists (Clar); the chokepoint
(seam comment in focus.c) is ready.

The LF commit adds:

- `<intuition/intuition.h>` — V36+ `struct IntuiMessage` (ExecMessage
  prefix + Class/Code/Qualifier/IAddress/MouseX/MouseY/Seconds/Micros
  /IDCMPWindow). `intuition.h` now includes `<exec/ports.h>` for the
  `struct Message` prefix.
- `Leargas_BuildIntuiMessage` (window.c, dual-target, pure) — maps a
  flat `LeargasInputEvent` to an IntuiMessage: IECLASS_RAWKEY →
  IDCMP_RAWKEY, copies Code/Qualifier, window-relative MouseX/MouseY
  (screen pointer minus window origin), the seconds/micros split of
  `ie_ts_ns`, and the IDCMPWindow back-pointer.
- Per-window UserPort (window_alloc.c): OpenWindow creates a
  `KOBJ_MSGPORT` (`Croi_AllocSignal` + `Croi_CreateMsgPort`, cap
  `LEARGAS_IDCMP_RING_CAP = 16`) when `IDCMPFlags != 0` and a task
  context exists; CloseWindow drains + frees any pending IntuiMessages,
  destroys the port, and frees the signal. `LeargasWindow` gains
  `idcmp_sigbit` (−1 when portless). The pre-Sched_Init boot demo
  window opens portless (AllocSignal returns −1) and keys are dropped
  until a post-scheduler client owns a port.
- Router hook (router.c): a `Leargas_KeyRouteFn` function pointer kept
  in the dual-target router (no kernel dep); RAWKEY events call the
  installed hook with the active window. Unset (host builds, the
  router unit test) → the pre-LF drop behaviour.
- idcmp.c (kernel-only): `Leargas_IDCMP_RouteKey` (the default hook —
  alloc + Build + `Croi_PutMsg`; refuses null window / no UserPort /
  no IDCMP_RAWKEY / full ring, freeing on failure), and the consumer
  side `Leargas_IDCMP_GetMsg` / `Leargas_IDCMP_DisposeMsg`. Phase 1
  has the consumer free the message; Phase 3 swaps DisposeMsg for the
  V36+ `ReplyMsg` round-trip.
- entry.c installs the hook (`Leargas_SetKeyRouter`) at boot.
- `tests/unit/test_leargas_idcmp.c` (host, test 18/18) — the pure
  translation: class map, field copy, window-relative coords,
  timestamp split, no-screen fallback, null-safety.
- `src/croi/tests/test_idcmp.c` — `KERNEL_TEST(idcmp_rawkey)`, the
  full path: OpenWindow gets a UserPort + focus via WFLG_ACTIVATE;
  direct RouteKey → GetMsg field checks; rejection (no IDCMP_RAWKEY,
  null window); and the end-to-end Post → Drain → hook → focused
  window's port. 16/16 kernel tests pass under the QEMU boot smoke.

Remaining for Phase 1 Subgoal 6: LG/LH gadgets + string Inntin. The
HB.* HID Gleas (Phase 3 prerequisite) wires the Phase 3 producer
side onto the same L0 contract — no L0 changes needed when it lands.

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
