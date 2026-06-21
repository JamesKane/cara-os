<!-- SPDX note: markdown is licence-exempt (docs/PRINCIPLES.md §1). -->
# Croi / devices — Phase 3 L6 design

> The L6 scoping doc, in the shape of `docs/LOGAIC_DOS.md` (L3),
> `docs/DATH_GRAPHICS.md` (L4), and `docs/LEARGAS_INTUITION.md` (L5). Read
> it before cutting L6 code. Pairs with `docs/ARCHITECTURE.md §6` (IPC),
> `docs/LVO.md` (dispatch), and `docs/PHASE3.md §L6` (the charter).
>
> Devices are **Croi**-owned (`PHASE3.md §L6`): the exec IO primitives and
> the device registry are kernel-resident. The shipped names are verbatim
> V36+ — `console.device`, `input.device`, `timer.device`, `OpenDevice`,
> `DoIO`, `struct IORequest`.

---

## 1. Scope

L6 adds the **exec device model** — the second AmigaOS dispatch mechanism
alongside libraries. A library call dispatches through a vec table by LVO;
a **device** request is a `struct IORequest` you fill with an `io_Command`
and hand to `DoIO()`, which routes it to the device that `OpenDevice()`
bound. L6 ships the exec IO primitives plus the three devices the apps +
intuition need.

**In scope (apps-driven):**
- **exec IO primitives**: `OpenDevice`/`CloseDevice`/`DoIO`/`SendIO`/
  `WaitIO`/`CheckIO`/`AbortIO` (the L1-deferred device verbs), `exec/io.h`
  (`IORequest`/`IOStdReq`/`Device`/`Unit`, `CMD_*`, `IOF_QUICK`).
- **`timer.device`**: `TR_GETSYSTIME`/`TR_SETSYSTIME`/`TR_ADDREQUEST` over
  `Croi_Time` — apps' timing/delay/double-click/animation clock.
- **`console.device`**: `CMD_WRITE` (→ the kernel console/log, reusing the
  L3.6 console path), `CMD_READ` (EOF stub) — under the dos console.
- **`input.device`** (minimal): `IND_WRITEEVENT` (inject) + `IND_ADDHANDLER`
  (register), bridging the Leargas input ring; the full handler chain is
  deferred (§2.5).

**Out of scope (deferred / later):**
- **Async IO with real reply-port queues** — v0 devices are synchronous
  (§2.2); `SendIO`/`WaitIO`/`CheckIO`/`AbortIO` degenerate.
- **The full input-handler chain** (intuition as an `input.device`
  handler) — Leargas drains the ring directly today (§2.5).
- **The device long tail** — `keyboard`/`serial`/`gameport`/`trackdisk`/
  `audio`/`clipboard.device`, the `*.resource`s — added as an app needs.
- **`keymap.library`** — Leargas's built-in RawkeyToAscii stands in.
- **`CreateIORequest`/`CreateExtIO`/`DeleteIORequest`** — these are
  **amiga.lib** (link-library) conveniences, not exec LVOs; provide as
  small helpers / let apps build IORequests + a reply MsgPort by hand
  (note for `PORTING.md`).

**The apps that drive L6** (`PHASE3.md §3`): all three want **timing**
(`timer.device` — delays, double-click, cursor blink); CLI-flavoured tools
want **`console.device`**; **`input.device`** underlies intuition (apps
rarely open it). So the L6 done-bar: *a V36 Gleas `OpenDevice`s
`timer.device`, `DoIO`s `TR_GETSYSTIME` + `TR_ADDREQUEST`, and writes text
through `console.device` `CMD_WRITE`* — plus ABI-complete declaration +
stub coverage.

---

## 2. The key decisions

### 2.1 Device model — a name-registered `KOBJ_DEVICE` with a kernel BeginIO

A CaraOS device is a small kernel descriptor (already reserved as
`KOBJ_DEVICE`, `kobj.h`):

```c
struct CaraDevice {
    struct Device pub;     // exec struct Device (a Library) — offset 0
    const char *name;      // "timer.device", …
    void (*beginio)(struct IORequest *io);   // command dispatch (kernel)
    BYTE (*open)(struct IORequest *io, ULONG unit, ULONG flags); // 0 = ok
    void (*close)(struct IORequest *io);
};
```

A small **device registry** (kernel table, populated at boot via
`Croi_Device_Register`) maps name → `CaraDevice *`. `OpenDevice(name,
unit, io, flags)` looks the name up, calls `dev->open`, sets
`io->io_Device = &dev->pub` / `io->io_Unit`, returns the error.

**Decision: `DoIO` calls `dev->beginio` directly** (a kernel function
pointer in the descriptor) rather than reconstructing the classic
device **vec table** (`BeginIO` at offset -30, `AbortIO` at -36). The
negative-vec device dispatch is an ABI detail the Phase-9 binary
translator cares about; for cleanroom C devices, a `beginio` fn pointer +
an `io_Command` switch is the same thing without the trampoline. (If a
future device must expose a real vec — e.g. an app that pokes
`io_Device->dd_Library` — we add it then.)

### 2.2 Synchronous v0 IO

The devices are synchronous (timer waits, console writes immediately, the
input ring is already drained), and the scheduler is cooperative
single-hart — so a `DoIO` is atomic the same way an L3.3 dos packet op is.
Decision: **`DoIO(io)` = `BeginIO` (run the command, set `io_Error` +
`io_Actual`) + return `io_Error`.** The async verbs degenerate:
- `SendIO(io)` → `BeginIO`, then `ReplyMsg` to `io_Message.mn_ReplyPort`
  (so a `WaitPort`+`GetMsg` on the reply port still works).
- `WaitIO(io)` → returns `io->io_Error` (already complete).
- `CheckIO(io)` → returns the request (done) / handles the QUICK case.
- `AbortIO(io)` → no-op (nothing in flight).
- `IOF_QUICK` is always effectively set.

`TR_ADDREQUEST` (wait until a time) blocks the caller via `Croi_Time` +
the cooperative scheduler (poll-yield, like `Delay`), which is the one
genuinely-blocking command. Real async IO (a device task + a request
queue) is a later refinement, behind the same `SendIO`/`WaitIO` API.

### 2.3 `timer.device` — over `Croi_Time`

`struct timerequest { struct IORequest tr_node; struct timeval tr_time; }`
(devices/timer.h). v0 commands:
- `TR_GETSYSTIME` → `tr_time` from `Croi_Time_Now` (ns → secs/micros).
- `TR_SETSYSTIME` → accepted, ignored (the monotonic clock isn't settable
  in v0; logged).
- `TR_ADDREQUEST` → wait `tr_time` (a duration) then complete — over
  `Croi_Time` (cf. `Delay`, L3.6).
- All units (`UNIT_MICROHZ`/`UNIT_VBLANK`/`UNIT_ECLOCK`) map to the one
  monotonic clock in v0; `UNIT_ECLOCK`'s `ReadEClock` granularity is the
  timebase.

### 2.4 `console.device` — over the L3.6 console

`CMD_WRITE` writes `io_Data`/`io_Length` to the kernel console (the L3.6
log-backed `cout` path); `CMD_READ` returns EOF (stub stdin, like dos);
`io_Actual` reports bytes written. The `CD_*` console specials
(`CD_ASKKEYMAP`, `CD_SETPRWINDOW`, raw/cooked mode) are deferred. This is
the device under the dos **console handler** — the dos console
FileHandle's writes and `console.device` `CMD_WRITE` reach the same sink.

### 2.5 `input.device` — minimal v0 (Leargas ring is the path)

Today Leargas drains the input ring directly (`router.c`); intuition is
**not** an `input.device` handler. A faithful `input.device` interposes a
handler chain (handlers get first crack at events; intuition is the
last). That's a real refactor. v0 keeps the Leargas ring as the input
path and gives `input.device` a thin surface:
- `IND_WRITEEVENT` → post the event(s) into the Leargas input ring
  (`Leargas_Input_Post`), so a synthetic-input client works.
- `IND_ADDHANDLER`/`IND_REMHANDLER` → record the handler (v0: stored, not
  yet invoked — the chain is deferred); logged so the gap is visible.
- `IND_SETTHRESH`/`IND_SETPERIOD` → accepted/ignored.

The real handler chain (and routing the ring *through* it) is a tracked
follow-up; it's the seam a commodities.library (L10–L14) input handler
would need.

### 2.6 Flavour: `syscall`, IORequests read kernel-side

The exec device primitives are `syscall` flavour (like the rest of exec's
syscall rows). `IORequest`s are app memory the kernel reads/writes with
SUM=1 (like dos's `FileInfoBlock *` / intuition's `NewWindow *`). The
device registry + every `beginio` run kernel-side. `OpenDevice` takes
4 args, `DoIO`/etc. 1 — all within the 7-register syscall ABI (no
marshalling stub needed).

---

## 3. exec IO primitives + registry

`exec/io.h` (NEW): `struct IORequest`/`IOStdReq` (Message prefix +
`io_Device`/`io_Unit`/`io_Command`/`io_Flags`/`io_Error` [+ `io_Actual`/
`io_Length`/`io_Data`/`io_Offset`]), `struct Device`/`Unit`, `CMD_*`
(0..9), `IOF_QUICK`/`IOB_QUICK`. LVO anchors (verify against the exec
autodoc/FD in `amiga_docs/`): `OpenDevice -444`, `CloseDevice -450`,
`DoIO -456`, `SendIO -462`, `CheckIO -468`, `WaitIO -474`, `AbortIO -480`
— they slot into the existing `exec.conf` `##pad_run`s (the generator
keeps declaration order aligned with the canonical numbers).

`Croi_Device_Register(struct CaraDevice *)` adds to a fixed kernel table;
`entry.c` registers timer/console/input at boot (after Sched_Init, like
the dos handler). `OpenDevice` matches by name.

---

## 4. Slice plan (dependency-ordered, each ends green + committed)

- **L6.1 — exec IO primitives + device registry.** `exec/io.h`; the
  `CaraDevice` registry (`Croi_Device_Register`/lookup); `OpenDevice`/
  `CloseDevice`/`DoIO`/`SendIO`/`WaitIO`/`CheckIO`/`AbortIO` (synchronous
  v0, §2.2). *Done when:* a KERNEL_TEST registers a trivial echo device,
  `OpenDevice`s it, `DoIO`s a command, and reads back `io_Error`/
  `io_Actual` — proving the open→dispatch→complete path.
- **L6.2 — `timer.device`.** `struct timerequest` + `TR_*`/`UNIT_*`
  (devices/timer.h); register the device; `TR_GETSYSTIME` + `TR_ADDREQUEST`
  over `Croi_Time`. *Done when:* `OpenDevice("timer.device")` + `DoIO`
  `TR_GETSYSTIME` returns a plausible time, and `TR_ADDREQUEST` for a
  short delay completes.
- **L6.3 — `console.device`.** `CMD_WRITE` → the kernel console (L3.6
  path); `CMD_READ` EOF. *Done when:* `CMD_WRITE` reports `io_Actual ==
  io_Length` and the text reaches the `cout` log.
- **L6.4 — `input.device` (minimal).** `IND_WRITEEVENT` → the Leargas
  ring; `IND_ADDHANDLER` recorded (chain deferred). *Done when:* an
  `IND_WRITEEVENT` event shows up in the ring (drained by the router).

**L6 done (epic):** `PHASE3.md §5` — `.conf`/`io.h` declare the documented
set at canonical numbers; a canonical V36 snippet (`OpenDevice` +
`DoIO`) compiles/links; the apps-driven commands have a `KERNEL_TEST`;
every unimplemented LVO/command is a logged stub the coverage report (LVO
side) or a `CMD`-default lists.

**Testing.** KERNEL_TESTs: a synthetic echo device (L6.1), then real
`DoIO`s against timer/console/input over off-screen/log sinks +
`Croi_Time`. Deterministic, no hardware (QEMU-first).

---

## 5. Open questions / deferred

1. **Async IO.** v0 is synchronous (§2.2). A device task + request queue
   (real `SendIO`/`WaitIO`/`AbortIO`) lands when a device genuinely
   blocks off-hart (e.g. a future `serial.device`/`trackdisk.device`).
2. **Input-handler chain.** v0 `input.device` records handlers but Leargas
   drains the ring directly (§2.5). Interposing the chain (intuition as
   the terminal handler) is the seam commodities (L10–L14) needs.
3. **`CreateIORequest`/`DeleteIORequest`/`CreateExtIO`.** amiga.lib, not
   exec — apps build an IORequest + reply port by hand in v0, or we add a
   tiny helper. Document in `PORTING.md`.
4. **Device units.** v0 treats the unit number as advisory (one unit per
   device). Multi-unit devices (e.g. `trackdisk` drives) come with the
   device that needs them.
5. **`timer.device` settable clock / EClock.** `TR_SETSYSTIME` is ignored
   (monotonic-only v0); `ReadEClock`/`UNIT_ECLOCK` granularity is the raw
   timebase. A wall-clock RTC is a later board-bring-up concern.
6. **The device tail + resources.** keyboard/serial/gameport/audio/
   trackdisk/clipboard + `*.resource` are apps-gated additions on the same
   `CaraDevice` model.
