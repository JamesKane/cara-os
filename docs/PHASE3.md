# Phase 3 — AmigaOS Release 2 (V36+) parity

> The Phase 3 plan document (`docs/ROADMAP.md` Phase 3). Phase 2 gave us
> persistent storage; Phase 3 is the project's real charter: the AmigaOS
> Release 2 API surface, under its **verbatim** names, so that
>
> > a well-written Release 2 (V36+) application source listing builds
> > against CaraOS headers and runs as a Gleas without source edits.
>
> Spec source: the 3rd Edition RKMs (Includes & Autodocs, Libraries,
> Devices) — the PDFs in `amiga_docs/`. Cleanroom rule (PRINCIPLES §2):
> read the spec, write our own. The dispatch model is `docs/LVO.md`; the
> namespace rule is PRINCIPLES §3.1; the Kobj/IPC model is
> ARCHITECTURE §5/§6.

---

## 1. The three decisions that shape this plan

1. **Breadth-first, library-by-library.** We bring one library to its
   target bar, green, before starting the next — in dependency order
   (§4). Easier "is this library done?" tracking; the runnable
   end-to-end app arrives late (epic A), accepted.
2. **`dos.library` is an AmigaDOS handler Gleas.** `dos.library` calls
   become `DosPacket`s `PutMsg`'d to a Logaic **handler Gleas** that owns
   the CaraFS mount (the `server` LVO flavour, ARCHITECTURE §6, CARAFS §4).
   This retires the Phase-2 `Croi_Fs_*` stopgap syscalls.
3. **Apps-driven implementation, ABI-complete declaration.** Each
   library ships its **full** `.conf` + headers first — every documented
   V36+ LVO at its canonical number/signature/struct shape, so a V36
   program always *compiles and links* (the literal criterion). Function
   **bodies** are implemented to what the representative apps (§3)
   exercise; the rest are defined stubs (return a documented failure /
   sane default, logged once) filled in as a real program hits them.

The reconciliation: **link-compatibility is exhaustive; runtime
behaviour is apps-driven.** A stub is a known, greppable gap, never a
missing symbol.

---

## 2. Architecture recap (what every library epic reuses)

- **Namespace split (PRINCIPLES §3.1).** `include/exec/*`, `dos/*`,
  `intuition/*`, … are verbatim V36+ (exact `OpenLibrary`, `tc_Node`,
  LVO numbers). The CaraOS-branded module (Croi/Logaic/Leargas/Dath) is
  the *implementation team*, not the shipped name. They meet only at the
  LVO trampoline.
- **LVO-gen (`docs/LVO.md`).** `tools/lvo-gen/<lib>.conf` is the source
  of truth → `proto/<lib>.h` stubs, `<lib>/lvo.h` constants,
  `src/<owner>/<lib>_vec.c`, and the aggregated table. Each LVO declares
  a flavour: `local` (in-proc), `syscall` (ecall into Croi), `server`
  (PutMsg to a Gleas). Never hand-edit generated files.
- **Existing slices to widen, not restart.** `exec.library` (17 LVOs,
  `src/croi/exec_lib`), `intuition.library` (5 LVOs over Leargas,
  `src/croi/intuition_lib`), and the Dath CPU rasteriser already exist
  and prove the pipeline. Phase 3 widens them.
- **Kobj/Handle model (ARCHITECTURE §5).** New typed kernel objects
  (semaphores, devices, dos locks/handlers) are `Kobj`s; public calls
  return canonical pointers (`struct Library *`, `BPTR`, `struct
  MsgPort *`), valid anywhere thanks to SASOS.

---

## 3. The yardstick: three representative apps

"Apps-driven" needs concrete apps; the criterion names a text editor,
paint, and file manager. We rebuild these from canonical-style V36
source and they define each library's target bar:

- **Editor** (an `Ed`/CygnusEd-flavoured text editor) — exercises
  `dos` (Open/Read/Write/Seek), `intuition` (window, menus, string +
  scroller gadgets, IDCMP), `graphics` (text), `asl` (file requester),
  `gadtools`, `utility` (tags).
- **File manager** — `dos` (`Lock`/`Examine`/`ExNext`/`Info`/`DeleteFile`/
  `Rename`), `intuition` + `gadtools` (list/button gadgets), `asl`,
  `icon` (`.info`).
- **Paint** — `intuition`, `graphics` (RastPort draw/fill/blit/areas),
  `asl`, `iffparse` (ILBM load/save), `diskfont` (text tool).

A library is "done for Phase 3" when its apps-driven functions are
implemented + tested and the apps that need them build and run.

---

## 4. Epic breakdown (dependency order)

Each epic ends green (host ctest + kernel smoke + format-check) and
commits per the standing rule. "ABI" = full `.conf` + headers
(link-complete); "impl" = apps-driven bodies + tests; "stub" = declared,
returns a documented default until a program needs it.

### P0 — ABI / toolchain proof
A canonical V36 source app (an OpenLibrary + OpenWindow "hello") builds
with host clang against `include/{exec,intuition,...}` **unmodified** and
runs as a Gleas. Seed `docs/PORTING.md`. Mostly validates what exists;
the falsifiable point — *verbatim V36 source compiles against our
headers* — must hold before we invest in breadth.

### L1 — `exec.library` (Croi) — the substrate
ABI: the full V36 Exec autodoc. Impl: lists (`AddHead`/`AddTail`/
`Remove`/`Enqueue`/`FindName`…), memory (`AllocMem`/`AllocVec`/pools),
tasks/process basics, ports + messages (`CreateMsgPort`/`PutMsg`/`GetMsg`/
`WaitPort`/`ReplyMsg`), libraries (`OpenLibrary`/`MakeLibrary`/`SetFunction`),
**semaphores** (`ObtainSemaphore`…), and the **device** primitives
(`OpenDevice`/`DoIO`/`SendIO`/`CheckIO`/`WaitIO`/`AbortIO`) that L6 builds
on. Everything else calls Exec, so it goes first and goes deep.

### L2 — `utility.library` (Croi)
ABI + impl: tag-list helpers (`GetTagData`, `FindTagItem`, `NextTagItem`,
`CloneTagItems`, `MapTags`, `PackBoolTags`…) and the `Hook`/callback
helpers. Small, pure, and a prerequisite for every tag-driven V36 API
(`OpenWindowTagList`, gadtools, asl), so it lands right after Exec.

### L3 — `dos.library` (Logaic) — the handler Gleas
ABI: the full V36 dos autodoc. Architecture: a Logaic **handler Gleas**
owns the CaraFS mount and receives `DosPacket`s; `dos.library` LVOs are
`server`-flavour (PutMsg round-trip) for the packet ops and `local`/
`syscall` for the rest. Impl: `Lock`/`UnLock`/`DupLock`,
`Open`/`Close`/`Read`/`Write`/`Seek`, `Examine`/`ExNext`/`ExAll`,
`CreateDir`/`DeleteFile`/`Rename`/`SetProtection`/`SetComment`/
`SetFileDate`, `Info`, plus the process/CLI bits the apps need
(`Output`/`Input`, `Delay`, `IoErr`). A **console handler** gives
stdin/stdout. **Retires `Croi_Fs_*`** (delete the G3 syscalls + repoint
Clar). BPTR/BSTR boundary handled here.

### L4 — `graphics.library` (Dath)
ABI: the V36 graphics autodoc (the Phase-4 GPU binding stays out —
ARCHITECTURE/ROADMAP: Phase 3 is the canonical API + the CPU
rasteriser). Impl: RastPort drawing (`Move`/`Draw`/`RectFill`/`WritePixel`/
`Flood`/areas), `BitMap`/`View`/`ViewPort`, text (`Text`/`SetFont`), and
the blits intuition + paint use. Built on the existing Dath blitter.

### L5 — `intuition.library` (Leargas) — widen
ABI: the V36 intuition autodoc. Impl beyond today's 5 LVOs: screens
(`OpenScreen`/`OpenScreenTagList`), the tag window opener, menus
(`SetMenuStrip`/`Menu`/`Item`), requesters (`Request`/`AutoRequest`/
`EasyRequest`), the full gadget + IDCMP surface, `Intuition` ticks. Tag
openers need L2; rendering needs L4.

### L6 — Devices (Croi) — console / input / timer first
ABI + impl as `KOBJ_DEVICE`s reached via Exec's L1 device primitives:
`console.device` (under the dos console handler), `input.device` +
`timer.device` (intuition timing/IDCMP), then the long tail
(`keyboard`/`serial`/`gameport`) as apps need. Bridges the existing
Leargas input ring into the canonical `input.device` path.

### L7 — BOOPSI (Leargas)
ABI + impl: class/instance/dispatcher protocol; root, gadget, image
classes. Prerequisite for gadtools.

### L8 — `gadtools.library` (Leargas)
ABI + impl: `CreateGadget`/`NewGadget`, menu helpers, list/cycle/string/
button/slider/scroller gadgets, `GT_*` tags. The apps' gadget toolkit.

### L9 — `asl.library` (Leargas)
ABI + impl: file requester, font requester, screen-mode requester
(tag-driven). Editor + file-manager + paint all want the file requester.

### L10–L14 — the V36 long tail (apps-driven)
`iffparse.library` (ILBM for paint), `icon.library` (`.info` ↔ the
`cara.icon` xattr from CARAFS §3.10), `diskfont.library`,
`commodities.library`, `expansion.library` (FDT-backed AutoConfig
analogue). Each ABI-complete; impl only what an app exercises.

### T — Standard tools (Gleasanna)
`Ed`, `List`, `Copy`, `Type`, `Info`, `Status`, `Search`, `Format`, … —
small Gleasanna over dos/utility. They also dogfood L1–L3.

### A — Apps + `docs/PORTING.md` (the criterion proof)
Rebuild the three representative apps from canonical V36 source with no
edits; document the rebuild-and-link recipe. When they run, Phase 3's
criterion is met (under QEMU; real-hardware sign-off as ever).

---

## 5. Per-epic done-criteria (falsifiable)

For every library epic: (a) the `.conf` declares the full documented LVO
set with canonical numbers (a host check cross-references the autodoc
LVO constants); (b) `proto/<lib>.h` + the API headers compile clean and
a canonical V36 snippet using the library links; (c) the apps-driven
functions have host unit tests and/or a `KERNEL_TEST`; (d) every
unimplemented LVO is a logged stub, not a missing symbol — a
`stub-coverage` check lists exactly which LVOs are stubs so the gap is
always visible.

---

## 6. Out of scope for Phase 3 (ROADMAP)

- 68k binary compatibility (Phase 9 translator).
- The GPU binding under `graphics.library` (Phase 4); Phase 3 ships the
  API + CPU rasteriser.
- `audio.library` / sound (Phase 5).
- Networking (Phase 5/7).
- `mathieee*` beyond a thin shim forwarding to RV64 IEEE-754.
- Outline-font quality past what `diskfont.library` formally requires.

---

## 7. Open questions

1. **`proto/` placement.** LVO-gen emits `proto/<lib>.h`; confirm the
   generated-header search path so app source `#include <proto/dos.h>`
   resolves in both the host app-build and the Gleas build.
2. **BSTR/BPTR.** ✅ **RESOLVED (L3.1, `docs/LOGAIC_DOS.md` §2.1).**
   `BPTR` = a real pointer-width value (`void *`, no `>>2` shift) —
   conforming to the in-tree `typedef void *BPTR` in `<exec/types.h>`;
   `BADDR`/`MKBADDR` are identity casts. `BSTR` widened to `BPTR`.
   Conversion only at the dos.library boundary.
3. **Process vs Task.** ✅ **RESOLVED (L3 scope, `docs/LOGAIC_DOS.md`
   §2.2).** A U-mode Gleas's `struct Task` is embedded at the front of a
   `struct Process` allocated in the SASOS shared heap, so
   `(struct Process *)FindTask(NULL)` is legal in U-mode — this also
   fixes the L1 FindTask-opacity gap. kmain/kernel tasks stay
   kernel-resident.
4. **Stub policy granularity.** ✅ **RESOLVED (L1).** Per-LVO: every
   unimplemented slot is an explicit `##pad_run` row pointing at
   `Croi_LvoUnimplemented`, so the runtime ordinal of every real function
   stays frozen as the surface fills in. The uniform report is
   `docs/LVO_COVERAGE.md`, generated by `lvo-gen --coverage` (single
   source of truth = the `.conf` set); regenerate with `cmake --build
   build-host --target lvo-coverage`. Classifies each slot impl / server /
   stub / reserved and reports per-library coverage %.
5. **Which editor/paint/file-manager.** Pick the canonical-source apps
   for epic A early (they set the bars); candidates are clones written to
   the 3rd-Edition idioms rather than specific shareware binaries.
