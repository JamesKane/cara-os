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

> **Status:** T.1 ✅ (`c6ae1f2`, the libc), T.2 ✅ (`bc87a9c`, Dhrystone 1.1
> builds + runs unedited), **T.3 ✅ — the AmigaDOS launch path COMPLETE**:
> T.3.1 console input (`72efe5a`), T.3.2 LoadSeg + RunCommand + argv
> (`e35f594`), T.3.3 CaraShell / boot-to-shell (`6bd245e`). **T.4 is next —
> first real GUI app, candidate chosen = `amiCalc`** (scoped below).

### T.1 — the userland libc + SDK harness ✅

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

### T.2 — first real app: a console (dos-only) tool ✅

- Vendored **Dhrystone 1.1** (`ports/dhrystone/`, verbatim) — built
  unmodified with `cara_port_flags` + `cara_user_libc`, runs under QEMU and
  prints its result. Forced: `cara_port_flags` (gnu89 port toolchain),
  `<sys/times.h>`/`times()` over `SYS_CurrentTime`, the `ports/` pattern.
- **Caveat it surfaced (→ T.3):** the program is *embedded* (`.incbin`) and
  *spawned by a `KERNEL_TEST`* — that is a test-harness shortcut, **not how
  a real OS launches a program.** A real launch is: a Shell reads a command
  line from a console, `LoadSeg`s the binary from the CaraFS volume, and
  `RunCommand`s it as a Process with its argument string → `argv`. None of
  that exists yet (§6). The embed path stays valid for headless self-tests;
  the Shell is the real, interactive path.

### T.3 — the AmigaDOS launch path: Shell + LoadSeg + RunCommand + argv (§6)

The missing layer T.2 revealed. Sliced:

- **T.3.1 — console input.** Keyboard → a reading Process's `Input()`
  stream (today dos stdin is hardwired to EOF; HID reaches Leargas but no
  console-input handler exists). A `CON:`-style line discipline (echo,
  backspace, Enter) so `Read(Input(), …)` / `FGetC` return typed bytes.
  Test: a Gleas reads a line and echoes it (keystrokes injected into the
  input ring by a `KERNEL_TEST`, or via QEMU stdin).
- **T.3.2 — LoadSeg + RunCommand + argv.** `LoadSeg`/`UnLoadSeg` (read an
  ELF file off CaraFS via dos, hand it to `Croi_LoadElf`); `RunCommand`/
  `CreateNewProc`/`SystemTagList` (spawn a loaded segment as a child
  Process with a command tail, standard streams, current dir, `pr_CLI`);
  `libcara` `_start` builds `argc`/`argv` from the command tail. Test: a
  Gleas `LoadSeg`s + `RunCommand`s `dhrystone` *from a CaraFS file* (not the
  embed) with an argument string and checks it ran — the real launch path,
  minus the interactive shell.
- **T.3.3 — the Shell + boot-to-shell (the milestone).** A userland `Shell`
  Gleas: open a console, loop { prompt; read a line; parse; builtin
  (`cd`/`dir`/`echo`) or `LoadSeg`+`RunCommand`; }. `entry.c` boots it as
  the foreground CLI with a console window. **Done-bar:** boot CaraOS, get a
  `>` prompt, type `dhrystone`, and watch the real benchmark run — a program
  launched the way a real OS launches programs.

### T.4 — first real GUI app: **amiCalc**

**Candidate chosen (2026-06-24): `amiCalc`** — github `713avo/amiCalc`, **MIT**
(© 2025 moneyland), a single-file (1796-line) AmigaOS scientific calculator.
Picked after vetting real candidates against our U-mode GUI surface:

| Candidate | License | GUI deps | Verdict |
|---|---|---|---|
| **713avo/amiCalc** | **MIT** | stock `intuition` + `graphics` only | **chosen** |
| alexalkis/acalc | none | gadtools ✓ but **GMP + MPFR + libm** | dependency dealbreaker |
| monopoldesign/GadToolsTest | murky (GadToolsBox) | gadtools incl. **LISTVIEW** | license + a deferred gadget kind |
| Aminet clocks (AnalogClock…) | freeware | — | binary-only, no source |
| AROS examples / RKM demos | APL / Commodore | stock | build-heavy / example-code, not a real app |
| hdpart | MIT | raw RDB/device IO | wrong domain |

**Why amiCalc fits.** Its GUI is **pure `intuition` + `graphics`** — it
`OpenWindow`s, draws its own button grid + display with `RectFill`/`Text`, and
runs a `WaitPort`/`GetMsg` IDCMP mouse loop doing its own hit-testing. **No
gadtools, no MUI, no slider/listview/palette, no `DrawImage`.** That is exactly
the surface we already support (the T.4 capability survey: window + IDCMP +
graphics draw all work), so the *GUI* should validate immediately rather than
forcing GUI substrate. It is also the strongest "port a real third-party app"
story since Dhrystone: a genuine, MIT-licensed, single-file application.

**What it forces (the substrate this slice builds).** amiCalc is *scientific*,
so its cost is **numerical**, not GUI:

1. **U-mode FPU.** Kernel `-march` is `rv64imafdc` (FP instructions present) but
   `-mabi=lp64` (soft-float ABI) and `user_task_trampoline` sets `sstatus` =
   `SPIE|SUM` only → **`FS=Off`**, so any U-mode `fadd.d`/`fmul.d` traps illegal.
   amiCalc computes in `double` → hardware FP instructions (the `d` is in the
   arch; no soft-float runtime is needed). So we must **enable FP for U-mode**
   (set `sstatus.FS=Initial/Clean` for user tasks) and **save/restore the FP
   register file** (`f0–f31` + `fcsr`) across `croi_ctx_switch`, since more than
   one task can now touch the FPU. (Dhrystone never hit this — it is integer.)
2. **libc float.** `fmt.c` currently emits a literal `"%f"` placeholder for
   `%f/%g/%e` (deferred since T.1). amiCalc uses `sprintf("%.15g", …)` (display)
   and `strtod` (input parse). So implement **`%f/%g/%e` formatting + `strtod`**
   in `cara_user_libc` (host-unit-testable — they are pure functions).
3. **libm.** amiCalc references **~11 transcendentals**: `sin cos tan asin acos
   atan exp log pow sqrt`. All must resolve to link the source *unedited*, even
   though basic `+ − × ÷` exercises none. Provide a small in-tree **`cara_libm`**
   (own implementations — no third-party libm linked, per `PRINCIPLES.md §2`;
   reading musl/openlibm to cross-check in tests is fine, linking is not). Basic
   accuracy (range-reduced poly / CORDIC) suffices; host unit tests assert error
   bounds vs a reference.
4. **Running a GUI app (the launch tension).** Today the Workbench screen comes
   up only on the *framebuffer* path (entry.c → `Leargas_OpenScreen` → Clar),
   while the **Shell runs on the *no-framebuffer* path**. amiCalc `OpenWindow`s
   on the Workbench screen, so a Shell-launched GUI app needs *both* a screen
   and the shell. Resolve by letting the boot bring the Workbench screen up
   whenever a framebuffer is present **and** run the Shell, so the console shell
   can launch GUI apps onto the screen (`run amicalc`). For the **green gate**
   (headless CI has no display), the GUI test uses a **synthetic in-RAM
   framebuffer**: a `KERNEL_TEST` allocates a RAM `g_fb`, opens a screen on it,
   spawns amiCalc, and asserts the window opened + the display buffer is
   non-blank (rendered). A stretch assertion injects pointer clicks ("2 + 2 =")
   and reads back the rendered "4". This keeps T.4 testable without a real
   display while the interactive boot (ramfb + USB mouse + `run amicalc`) is the
   live demo.

Plus the usual **vendoring**: `ports/amicalc/amicalc.c` *verbatim* upstream +
`PROVENANCE.md` (MIT text + commit) + a BSD-2 `CMakeLists.txt` built with
`cara_port_flags` + `cara_user_libc` + `cara_libm` + `libcara_user`, embedded +
launchable like dhrystone — the dhrystone recipe (§6, `ports/dhrystone/`).

#### Slice plan

- **T.4.1 — U-mode FPU.** Enable `sstatus.FS` for user tasks (trampoline) and
  add FP save/restore (`f0–f31` + `fcsr`) to the context switch; lazy-FP (set
  `FS=Initial`, save on switch only if dirtied) is the target, eager is the
  acceptable v0. `KERNEL_TEST` (and/or a tiny U-mode Gleas) proves a U-mode
  `double` computation survives a context switch. Kernel substrate; no app yet.
- **T.4.2 — libc float.** `%f/%g/%e` in `fmt.c` + `strtod` in `cara_user_libc`;
  host unit tests (`tests/unit/`) for formatting + round-trip parse. Pure, fast.
- **T.4.3 — `cara_libm`.** The ~11 transcendentals (own impls), a new static lib
  ports link; host unit tests assert accuracy vs a reference within a tolerance.
- **T.4.4 — vendor + run amiCalc (the milestone).** Drop `amicalc.c` under
  `ports/amicalc/` unedited; build it; bring the Workbench screen up alongside
  the Shell when a framebuffer exists; run it. Green gate: a synthetic-RAM-fb
  `KERNEL_TEST` opens the screen, spawns amiCalc, asserts it opened its window +
  rendered (stretch: click "2+2=" → "4"). Demo: boot with ramfb + USB mouse →
  `run amicalc` from the shell.

**Done-bar (the phase milestone):** a third-party AmigaOS GUI program, unedited
at the source level, opens its window and computes on CaraOS — the gaps it
forced (U-mode FPU, float libc, libm) filled, the GUI surface validated.

Subsequent slices port progressively larger/realer apps (an editor, a viewer),
each pulling forward the substrate it needs — the L1–L14 apps-driven rhythm, now
driven by *external* code.

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

---

## 6. The missing launch layer (what T.3 builds)

T.2 ran a real program, but the way it ran is a tell: the ELF is `.incbin`'d
into the kernel image and a `KERNEL_TEST` calls `Croi_SpawnUserTaskFromElf`
on the embedded blob. That is fine for an automated, headless self-test —
but it is **not how an operating system launches a program**, and it
quietly skips the layer that makes CaraOS feel like a system you can use.

On AmigaOS, `dhrystone` runs because the **Shell** does this:

1. it owns a **console** (`CON:` / a console window) that is its `Input()`
   and `Output()`;
2. it prints a prompt and **reads a command line** from that console;
3. it parses the line into a command + a **command tail** (the argument
   string);
4. it resolves + **`LoadSeg`s** the executable **off the volume** into
   memory (the dos loader / relocation);
5. it **`RunCommand`s** the segment as a child **Process** — installing the
   command tail (which the program's startup turns into `argc`/`argv`),
   the standard streams, the current directory, and `pr_CLI`;
6. it waits, reaps the child, and loops.

CaraOS today has **none** of steps 1–6. Concretely: dos `Input()` is
hardwired to immediate EOF (no console input — keyboard reaches Leargas but
no console handler feeds a reading Process); `LoadSeg`/`UnLoadSeg`,
`RunCommand`/`CreateNewProc`/`SystemTagList`, and `Execute` are all
unimplemented; and `libcara`'s `_start` calls `main(void)` — there is no
command tail → `argv`. So a program can only be *spawned by the kernel from
an embedded blob*, never *launched by a user from a console*.

This is the layer that L3 (dos) deliberately deferred — the process-launch
half of AmigaDOS — and it is exactly what "run a real app" needs to be real.
T.3 builds it (console input → `LoadSeg`-from-CaraFS → `RunCommand` + `argv`
→ a `Shell`), so the milestone becomes: **boot CaraOS, get a prompt, type
`dhrystone`, watch it run.** The embed path remains for headless tests; the
Shell is the real, interactive path a user (and a ported app's own
`System()`/`Execute()` calls) takes.
