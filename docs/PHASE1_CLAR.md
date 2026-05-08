# Phase 1 Subgoal 7 — Clar (Workbench)

> Plan for Clar, the Phase 1 minimum Workbench analogue. Pairs with
> `docs/ROADMAP.md` Phase 1 Subgoal 7 (the one-line requirement),
> `docs/PHASE1_LEARGAS.md` (the window-system substrate Clar runs
> on), `docs/PHASE1_FRAMEBUFFER.md` (the rasteriser),
> `docs/PHASE1_USB.md` (the input pipeline), and
> `docs/ARCHITECTURE.md` §11 (Clar is a Gleas — a normal U-mode
> program — not a kernel module).

---

## Status — 2026-05-08

**Not started.** Clar is the Phase 1 success-criterion artefact:
when Clar runs, Phase 1 ships. Every dependency below it (Subgoals
2–6) must be in place first.

---

## Context

`docs/ROADMAP.md` Phase 1 Subgoal 7 says:

> **Clar (Workbench).** Background screen, at least one drawer
> (Bosca), can open and close it with the mouse, can type into a
> simple text Inntin (gadget).

Clar is the Phase 1 *user-facing* artefact: a CaraOS user boots
the system, sees the Clar desktop, double-clicks a drawer, sees
its contents, clicks a text Inntin, and types into it. That is
the smallest demo that proves the whole Phase 1 stack works
end-to-end.

**Clar's relationship to Workbench.** Workbench (V36+) is a
Gleas — a normal program that uses `intuition.library` like any
other application. Clar is the same: a U-mode CaraOS Gleas that
runs `OpenScreen` / `OpenWindow` / `AddGadget` / `WaitPort` calls
through the V36+ LVO surface and draws drawer icons / window
chrome / text input via the public API. Clar is not privileged.
The kernel doesn't know Clar exists; the user can replace it.

**Bosca, Inntin, and the brand vocabulary.**
`docs/ARCHITECTURE.md` §11 / 13 lists the Irish-derived
brand-namespace names: `Bosca` ("box / drawer") for an
on-screen drawer / folder representation; `Inntin` ("intent")
for a gadget the user can manipulate. These are the *brand*
names; the public API uses canonical V36+ vocabulary
("drawer" / "gadget"). Inside Clar's source we use the brand;
inside the LVOs we use V36+.

### Strategy

**Smallest thing that satisfies the success criterion.** Reading
the Subgoal 7 line literally:

1. *Background screen* — one screen, one colour fill (or simple
   pattern). Phase 1 doesn't need wallpaper.
2. *At least one drawer (Bosca)* — exactly one is sufficient.
   Phase 1 doesn't need an icon grid or DnD.
3. *Can open and close it with the mouse* — single-click vs.
   double-click distinction is V36+ (single = select, double =
   open); Phase 1 picks one and documents it.
4. *Can type into a simple text Inntin* — one string gadget
   inside the opened drawer's window, accepting ASCII input
   with backspace and Enter.

That's it. Anything past it is Phase 3 / future:

### Out of scope for Phase 1's Clar

- **More than one drawer.** A grid of drawers, drag-and-drop,
  drawer-of-drawers nesting — Phase 3 / future.
- **Non-drawer icons (tools, projects, disks).** Phase 3.
- **Information windows, snapshot, format-disk, etc.** — All
  the V36+ Workbench menu options. Phase 3 + Phase 5.
- **Icons drawn from `.info` files.** `icon.library` is Phase 3;
  Phase 1 Clar uses a hand-coded drawer glyph.
- **Font selection / customisation.** Uses the Phase 1 8×8
  bitmap font Dath ships.
- **Multitasking demo (running a Gleas from Clar).** Phase 1 Clar
  doesn't shell out; it's a self-contained Gleas. Spawning more
  Gleasanna from Clar arrives with `dos.library` (Phase 3).
- **Persistent state.** No on-disk drawer contents in Phase 1
  (no filesystem yet — Phase 2). The single drawer's contents
  are hard-coded in the Clar binary.
- **The "scribble in a window" loose-leaf interaction in
  Workbench.** Replaced by the explicit Inntin text-input
  surface above.

### Where Clar lives

```
src/userland/clar/
  CMakeLists.txt
  clar.c                  main(); the boot-time Gleas entry
  clar_screen.c           Workbench-screen open + background paint
  clar_bosca.c            drawer model + visual + click handling
  clar_inntin.c           text-input gadget wiring inside the open drawer
  glyphs/
    drawer_closed.c       16x16 drawer glyph, generated like the font
    drawer_open.c
```

Clar links against `libcara_user` (the same C runtime userexec
uses) plus the future `libintuition_user` shim that wraps
`intuition.library`'s LVOs into application calls.

---

## Tier 1 — The Clar shell

**Exit:** the kernel boots Clar as a U-mode Gleas; it opens the
Workbench screen, paints the background, and displays one
drawer glyph at a fixed position. No interaction yet — the
program just sits in `WaitPort` on a never-arriving message
until Tier 2 adds input.

### Epic CA — Boot integration

**Goal:** Croi spawns Clar as the foreground Gleas after every
other Phase 1 boot work has settled.

- **CA.1** Embed `clar.elf` into `croi.elf` via the same `.incbin`
  pattern that `userhello.elf` and `userexec.elf` use. New
  `__clar_elf_start[]` / `__clar_elf_end[]` symbols.
- **CA.2** Boot path in `entry.c` — after `Croi_MakeLibrary` has
  brought up exec.library, intuition.library (Phase 3 Tier 1),
  and the input pipeline; spawn `Clar` via
  `Croi_SpawnUserTaskFromElf` at a default priority.
- **CA.3** kmain (the kernel-side foreground task) drops priority
  and idles in WFI; Clar gets the CPU.

### Epic CB — Workbench screen

**Goal:** Clar `OpenScreen()`s a single screen the size of the
framebuffer, paints it solid (Pen 0), and `OpenWindow()`s a
borderless full-screen "backdrop" window where the drawer glyph
will go.

- **CB.1** Screen open via `OpenScreen` LVO from
  `<proto/intuition.h>`. Tag-list configures the dimensions, the
  pen colour, the title (Phase 1: blank).
- **CB.2** Backdrop window via `OpenWindow` with
  `WFLG_BACKDROP | WFLG_BORDERLESS | WFLG_NOCAREREFRESH`. The
  backdrop window owns the screen-wide drawing area Clar paints
  into.
- **CB.3** Initial paint — the desktop is solid coloured;
  drawer glyph drawn at fixed position.

### Epic CC — Bosca model

**Goal:** the single drawer is represented as a hand-rolled
struct that holds its position, current state (open / closed),
glyph rendering, and the contents (the text Inntin).

- **CC.1** `struct ClarBosca { LONG x, y; bool open; struct
  Window *child; struct Gadget *inntin; ... }`.
- **CC.2** Closed-state render: blits `drawer_closed.c` glyph at
  `(x, y)` into the backdrop window's RastPort.
- **CC.3** Open-state render: a child window appears below the
  drawer; the closed glyph swaps for the open glyph.

---

## Tier 2 — Click to open / close

**Exit:** the user can click the drawer glyph to open the drawer
window, click the close gadget on that window to close it again.
Repeat any number of times.

### Epic CD — IDCMP loop

**Goal:** Clar's main loop is `WaitPort` on the backdrop window's
IDCMP port; on each message, dispatch by class.

- **CD.1** Open the IDCMP port with
  `IDCMP_MOUSEBUTTONS | IDCMP_CLOSEWINDOW | IDCMP_RAWKEY |
  IDCMP_GADGETUP` flags.
- **CD.2** Main loop pseudo-code:

  ```c
  for (;;) {
      WaitPort(window->UserPort);
      while ((msg = (struct IntuiMessage *)GetMsg(window->UserPort))) {
          ULONG cls = msg->Class;
          ReplyMsg((struct Message *)msg);
          switch (cls) { ... }
      }
  }
  ```

  Standard V36+ idiom; ReplyMsg before processing because we copy
  out the fields we need.
- **CD.3** A clean shutdown path on `IDCMP_CLOSEWINDOW` of the
  backdrop window (Phase 1: never triggered; the desktop has no
  close gadget).

### Epic CE — Bosca click handling

**Goal:** clicking inside the drawer glyph rect opens the drawer
window; clicking the close gadget on the drawer window closes it.

- **CE.1** Hit-testing on `IDCMP_MOUSEBUTTONS` events with the
  `MouseX` / `MouseY` from the IntuiMessage against the drawer
  glyph rect.
- **CE.2** Open: `OpenWindow` for the drawer's child window
  (titled "Bosca", contains the Inntin), repaint the closed →
  open glyph.
- **CE.3** Close: the child window's `IDCMP_CLOSEWINDOW` arrives
  on its own UserPort; close it via `CloseWindow`, repaint the
  open → closed glyph.
- **CE.4** Single-click vs. double-click: Phase 1 picks
  single-click for simplicity. (V36+ Workbench uses double-click;
  the distinction lives in the icon.library / Workbench
  preferences, neither of which are in Phase 1 scope.)

---

## Tier 3 — The Inntin

**Exit:** the open drawer window contains a string Inntin; the
user clicks it to focus, types ASCII characters which appear in
the gadget, presses Enter to log the typed text via
`SYS_LOG_WRITE`. The Phase 1 success-criterion demo is then
complete.

### Epic CF — String gadget

**Goal:** Clar adds a string Inntin to the drawer's child window
on open; removes it on close.

- **CF.1** `struct StringInfo` initialised with a 64-byte buffer.
- **CF.2** `struct Gadget` of type `STRGADGET` placed at a fixed
  position inside the drawer window.
- **CF.3** Add to the window via `AddGadget` LVO; remove via
  `RemoveGadget` on window close.

### Epic CG — Type & enter

**Goal:** ASCII keys typed while the Inntin is active are
captured by Leargas's gadget code (PHASE1_LEARGAS Epic LH) and
appear on screen; Enter posts `IDCMP_GADGETUP`; Clar reads the
buffer contents and logs them.

- **CG.1** The Leargas string-gadget code already handles the
  buffer manipulation and rendering; Clar only needs to
  `ActivateGadget` it on drawer-open so the gadget receives
  keystrokes immediately.
- **CG.2** On `IDCMP_GADGETUP` (the gadget's `GadgetID` matches
  Clar's known ID), read `StringInfo->Buffer`, log it via
  `SYS_LOG_WRITE` so the kernel log on the framebuffer / UART
  shows what the user typed. (`stdout`-style `printf` lands in
  Phase 6's `libcara` evolution; Phase 1 logs through Croi's log
  ring.)
- **CG.3** Re-`ActivateGadget` after the gadget-up so further
  keystrokes also reach it.

### Epic CH — End-to-end smoke

**Goal:** the QEMU smoke harness drives a scripted interaction
(move mouse to drawer, click, move to Inntin, click, type a known
string, press Enter) and asserts the typed string appears in the
log ring.

- **CH.1** Extend `tests/boot/smoke_qemu_kernel.sh` (or add a
  new script) to drive QEMU's `sendkey` / `mouse_move` /
  `mouse_button` monitor commands.
- **CH.2** Assert "clar: gadget got 'hello'" appears in the QEMU
  stdout log.
- **CH.3** That assertion replaces the Phase 1 success criterion's
  manual confirmation: when this test passes, Phase 1 ships.

---

## What this unblocks

- **Phase 1 ships.** Clar working under QEMU with virtio-input
  satisfies the success-criterion's QEMU equivalent. Real-hardware
  Splanc.efi boot (Subgoal 1) lets the same Gleas run on RV2.
- **Phase 2 (NVMe + CaraFS).** Clar's hard-coded single drawer is
  replaced by an actual filesystem-backed drawer that lists the
  CaraFS directory contents. The Phase 1 Clar code structure
  (one Bosca with one Inntin) generalises to "a Bosca per
  filesystem entry, each with a context-appropriate Inntin".
- **Phase 6 (on-target toolchain).** A user sitting at Clar opens
  a text editor (the Phase 3 `Ed` analogue) by clicking a tool
  icon in a drawer; the on-target compiler chain reads the file
  the editor saves. Phase 1 Clar is the bootstrap UX every later
  workflow lives inside.
