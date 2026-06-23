# Phase T — port a real Amiga application (plan)

The L1–L14 epics built the V36+ library surface and *proved* the ABI with
programs **we wrote** (`v36hello`, `userintuition`, `clar`). Those are
honest but self-graded: we wrote them to the API we built. The real test of
the contract — *"a well-written V36+ source program compiles for CaraOS
without edits"* (`CLAUDE.md`, `docs/PRINCIPLES.md §3.1`) — is to take an
**existing, third-party AmigaOS application's source code**, build it
against the CaraOS SDK with no API edits, and run it under QEMU.

That is Phase T: not writing our own editor/paint/file-manager, but
**finding a real Amiga C application and getting it to run.** The apps we'd
have written become, at most, fallbacks; the goal is external validation.

This doc is the phase plan (the role `docs/PHASE3.md` played for L1–L14).
The porter's step-by-step recipe lives in `docs/PORTING.md` and is updated
as Phase T fills the gaps below.

---

## 1. What "run a real app" actually requires

The recon found three substrate gaps between "we have the libraries" and
"a real app's source compiles + runs". These, not the app, are the work.

### 1.1 There is no libc (the gating gap)

Userland today is `-ffreestanding -nostdlibinc` with **only `memset`/
`memcpy`** (in `libcara_init.c`) — no `<string.h>`, `<stdlib.h>`,
`<stdio.h>`, `<ctype.h>`, no `printf`/`malloc`/`strcpy`/`sprintf`. Our
in-tree apps hand-roll `strlen` and build strings char-by-char. **Real
Amiga C source does not** — SAS/C and vbcc programs freely use the
compiler's libc + `amiga.lib`. Without a libc, real source will not even
compile, let alone link. So Phase T's first deliverable is a **minimal
CaraOS userland libc** layered over the Amiga API:

- `string.h` / `ctype.h` — pure logic (`strlen`/`strcpy`/`strcmp`/
  `memcmp`/`memmove`/`strchr`/`tolower`/…).
- `stdlib.h` — `malloc`/`free`/`calloc`/`realloc` over exec `AllocVec`
  (an Amiga app's `malloc` is classically AllocMem-backed anyway);
  `atoi`/`strtol`/`abs`/`qsort`/`rand`.
- `stdio.h` — `printf`/`fprintf`/`fputs`/`putchar` over the dos output
  stream (`Output()`/`Write`); `sprintf`/`snprintf`/`vsnprintf` (pure);
  a `FILE` shim over dos `Open`/`Read`/`Write`/`Close` for
  `fopen`/`fread`/`fwrite`/`fgets`/`fclose`. (Full buffered stdio is a
  stretch goal; line-oriented is enough for most ports.)
- `amiga.lib` glue we already have (`DoMethod`/`NewList`/…) + the bits a
  port needs (`CreatePort`/`CreateExtIO`/`BeginIO` helpers as required).

This is CaraOS-authored (BSD-2), part of the SDK — not third-party.

### 1.2 There is no out-of-tree SDK

Everything builds in the one CMake tree; the generated `proto/*.h` live in
`build-rv64/gen/`. A third-party app's source can't `#include <proto/
intuition.h>` from outside. Two options; Phase T takes the **vendored-in-
tree** one first (lowest friction), and leaves a real exported SDK as a
follow-on:

- **Vendored**: the app's source drops into `ports/<name>/` and builds in
  our CMake like another `src/userland` target (it sees `cara_headers` +
  the generated protos + `libcara_user` + the new libc). Fastest path to
  "it runs".
- **Exported SDK** (later): a `cara-sdk/` staging dir (frozen public
  headers + generated protos + `libcara`/libc archives + `user.lds` + a
  `cara-gcc`-style toolchain wrapper) so a port builds with its *own*
  unmodified Makefile. The truer validation, but more plumbing.

### 1.3 Loading + running the app ELF

Mechanics already exist: an RV64 ELF is embedded via `.incbin` into the
kernel `.user_elf` section (`user_blob.S`) and spawned by
`Croi_SpawnUserTaskFromElf` (boot path spawns `clar`; kernel tests spawn
the rest), and `Croi_LoadElf` can load an ELF blob from anywhere — so a
CaraFS-loaded path is a short reach. A ported app is "just another userland
ELF": embed it for the smoke/boot run, or load it from the CaraFS volume.

---

## 2. The key decisions

### 2.1 External source is third-party, not CaraOS code

A ported app is **not** CaraOS and is **not** linked into the kernel image
(it's a separate U-mode ELF the OS loads), so `PRINCIPLES §2` (no
third-party deps *in the image*) is satisfied. Its source is vendored under
`ports/<name>/` with **its own upstream license file**, exempt from the
BSD-2 SPDX-per-file rule (like `amiga_docs/`); only CaraOS-authored glue
(the libc, the CMake wiring, any port shim) carries BSD-2. We prefer apps
whose license permits vendoring a copy (public domain / BSD / MIT / GPL —
GPL is fine for a separately-built app); where a license forbids
redistribution we fetch-at-build instead of committing the source.

### 2.2 Selection criteria (a real app that *can* run)

A candidate must be:

1. **Pure C** — no 68k assembly, no 68k binary blobs (we cross-compile C →
   RV64; there is no 68k). Inline-asm-free or trivially shimmable.
2. **V36+ API** — uses `<exec/*>`/`<intuition/*>`/`<dos/*>`/… (the API
   namespace), not Workbench-1.x-only or AmigaOS-4-only calls.
3. **Within (or just beyond) our surface** — exec/dos/utility/intuition/
   graphics/gadtools/asl/icon/diskfont. "Just beyond" is good: the gaps it
   forces are the point (apps-driven, as in L1–L14).
4. **Free / redistributable source**, modest size.
5. **No deep device deps** — no serial/parallel/audio/network device.io we
   lack.

### 2.3 Where real license-clean V36 C source comes from

- **AROS** (AROS Public License / open) — its `examples/`, `tests/`, and
  small apps are pure C against the exact AmigaOS API, idiomatic, and
  redistributable. The primary well of "well-written V36 source."
- **Aminet public-domain / GPL C utilities** — classic third-party tools.
- (Not the RKM example listings in `amiga_docs/` — those are PDF images we
  may not copy, per `CLAUDE.md`.)

### 2.4 Graphics drawing already works (PORTING.md §5 is stale)

The recon's "graphics ABI-only, Phase 4" note reflects the **P0** PORTING.md
text, not reality: L4 graphics/Dath shipped pen+primitives, RectFill,
blits, **Text** (L12.1 strike renderer), Area* fill, and a live
`Screen.RastPort`. A ported GUI app *can* draw. Fixing PORTING.md §5 to the
current surface is part of T.1.

---

## 3. Slice plan

### T.1 — the userland libc + SDK harness

- A `libcara_c` (BSD-2) under `src/userland`: `string.h`/`ctype.h`/
  `stdlib.h` (malloc over AllocVec) / `stdio.h` (printf/sprintf/vsnprintf +
  a line-oriented `FILE` over dos). Wire it into the userland link
  alongside `libcara_user`.
- Establish `ports/` + the vendored-app CMake pattern (build an external
  source tree against `cara_headers` + protos + `libcara_user` + `libcara_c`,
  embed via `.incbin`, spawn like `clar`).
- Refresh `docs/PORTING.md` to the real current surface + the libc.
- **Test:** host unit tests for the pure libc bits (`sprintf`/`strtol`/
  `qsort`/string fns — they compile identically host + RV64, like the
  ring/FDT tests); a `KERNEL_TEST`/Gleas that `printf`s via dos Output and
  `malloc`s a buffer. No third-party app yet.

### T.2 — first real app: a console (dos-only) tool

- Recon + select the smallest license-clean real Amiga C CLI tool (exercises
  dos + the new libc only — no GUI), vendor it under `ports/`, build it
  **unmodified**, run it under QEMU reading/writing a CaraFS file.
- **Done:** the tool runs to completion with correct output; every API/libc
  gap it hits is filled (in the libc or the relevant library), tracked.
  This proves the toolchain + libc + dos path end-to-end on *foreign* code.

### T.3 — first real GUI app

- Recon + select a small license-clean intuition/gadtools app (a clock /
  calculator-class program — opens a window, gadtools gadgets, draws,
  IDCMP loop), vendor + build **unmodified**, run it under QEMU.
- **Done-bar (the phase milestone):** a third-party AmigaOS GUI program,
  unedited at the source level, opens its window and responds to input on
  CaraOS. The gaps it forces (a missing gadtools kind, a graphics call, an
  intuition tag, layers occlusion, the asl-font wiring) are filled or
  explicitly deferred.

Subsequent slices port progressively larger/realer apps (an editor, a
viewer), each pulling forward the substrate it needs — the L1–L14
apps-driven rhythm, now driven by *external* code.

---

## 4. Testing

- **libc** (T.1): host unit tests for the pure functions (sprintf/strtol/
  qsort/string/ctype); a Gleas exercising printf-over-dos + malloc.
- **ported apps** (T.2+): the app's own run *is* the test — boot it under
  the QEMU smoke harness and assert its output / a window-open marker /
  an exit code. Where an app has a non-interactive mode, drive it
  headless; for GUI, assert the window + a rendered-pixel/IDCMP marker via
  the existing screen-RastPort seam.

Every slice ends on the standing gate: host `ctest` green, in-kernel runner
`0 failed`, format-check clean, two-boot QEMU smoke `ok`; commit; regen
`docs/LVO_COVERAGE.md` if a `.conf` changed; handoff/memory follow-up.

---

## 5. Tracked gaps / risks

- **libc completeness** — full buffered stdio, `float` formatting in
  `printf`, locale, `setjmp`, signals: implement on demand as a port needs
  them; start line-oriented + integer.
- **lp64d / RVV** — userland is still `lp64` soft-float (`lp64d` is wired
  in the toolchain but unused). A port using `double` heavily may want
  `lp64d`; flipping userland to `lp64d` is a tracked sub-task.
- **No exported SDK** — T builds ports in-tree (vendored); a real
  `cara-sdk/` + toolchain wrapper (so a port builds with its own Makefile)
  is the truer validation and a follow-on.
- **Deferred library substrate an app may force forward** (from the L1–L14
  handoff): the input-handler chain (commodities live half / intuition-as-
  handler), `layers.library` (overlapping windows), `DrawImage`
  (planar→chunky), async device IO, gadtools LISTVIEW/PALETTE, the
  asl-font-requester→`AvailFonts` wiring, iffparse custom stream hooks /
  clipboard. A real GUI app is the likeliest forcing function for several.
- **Licensing** — verify each app's license permits vendoring; otherwise
  fetch-at-build. Keep third-party source clearly separated under `ports/`
  with upstream license intact.
