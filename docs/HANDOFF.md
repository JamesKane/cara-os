# CaraOS session handoff — 2026-06-08

> A pick-up-where-we-left-off note for a fresh session. Captures current
> state, the non-obvious decisions and gaps from the last sprint, the
> concrete next steps, and the build/test/commit workflow. Pairs with
> `docs/ROADMAP.md` (the phase plan), `docs/PHASE1_LEARGAS.md` and
> `docs/PHASE1_CLAR.md` (per-epic detail), and `docs/ARCHITECTURE.md`
> (the design).

---

## 1. Where we are

**Phase 1 Subgoal 6 (Leargas) is COMPLETE.** The **`intuition.library`
LVO surface (I1–I3) is now COMPLETE and proven end-to-end** — a real
U-mode Gleas opens the library and a window through the canonical V36+
LVOs. **Phase 1 Subgoal 7 (Clar) is the remaining work to ship Phase 1**,
and it now builds directly on this surface as the "proper" U-mode-Gleas
path the user chose.

Recent commits (newest first), all on `main`:

```
d2e8594 phase-3/I3  construct intuition.library + U-mode smoke (userintuition)
929fa3e tools       lvo-gen proto headers coexist across libraries
dfa0f16 phase-3/I2  intuition.library trampolines, dispatch + Leargas bridge
25270ad phase-3/I1  intuition.library conf + generated headers
25bcebd phase-3/S3  Leargas Screen/Window/IntuiMessage → shared heap
62d8c06 phase-3/S2  AllocMem → shared heap; userexec derefs it (U-mode proof)
fd9fd9b phase-3/S1  SASOS shared system heap (RW+U lower-half window)
87e4281 phase-1/LH  Leargas string Inntin  ← Subgoal 6 complete
```

Status: everything green — host `ctest` 20/20, in-kernel tests
**19 passed / 0 failed** (added `userintuition_smoke`), QEMU boot smoke
ok, `format-check` clean.

### What I2/I3 delivered (the intuition.library bridge is done)

- **I2** (`dfa0f16`): `.lib_text.intuition` trampolines folded into the
  shared `0x4000_0000` RX region (croi.lds), `SYS_AddGadget`..
  `SYS_ActivateGadget` (16..20) in `cara/sysno.h`, dispatcher arms in
  `syscall.c`, the five Leargas bridge bodies in
  `src/croi/intuition_lib/bridge.c`, reserved hooks in
  `intuition_hooks.c`, and `cara_intuition_lib` whole-archived into croi.
- **I3** (`d2e8594`): `entry.c` allocates `IntuitionBase` + its vec
  table in the SASOS shared heap and `Croi_MakeLibrary`s it (MKL_BASE
  alone — SUM=1 makes kernel and user views one pointer);
  `src/userland/userintuition.c` + `KERNEL_TEST(userintuition_smoke)`
  prove the chain (boot log: `registered 'intuition.library' V36.0`,
  `uintu … userintuition ok`).
- **lvo-gen fix** (`929fa3e`): `<proto/*.h>` headers now coexist in one
  TU (drop the `<lib>/lvo.h` include → literal ordinals; skip reserved-
  slot client stubs). Required because `userintuition.c` includes both
  `<proto/exec.h>` and `<proto/intuition.h>`. Also made cross-build
  regen depend on the lvo-gen binary (CaraLvoGen.cmake).

### Why the roadmap looks "out of order"

We are doing `phase-3/*` work (shared heap, intuition.library) *before*
Phase 1 fully ships, on purpose. Clar's success criterion ("type into a
text Inntin") is most faithfully met by a U-mode Clar using
`intuition.library`, and the user chose to build that foundation now
rather than ship an in-kernel stand-in. Phase discipline
(`PRINCIPLES.md` §6) is intentionally relaxed here per that decision.

---

## 2. The two milestones from this sprint (read before touching MM/IPC)

### 2.1 SASOS shared heap — the keystone (S1–S3)

`include/cara/shared.h`, `src/croi/mm/shared.c`. ARCHITECTURE §4.3/§4.4.

- A lower-half **RW+U** window at **`0x1_0000_0000`** (Sv39 `L2[4]`),
  backed by a fixed **8 MiB physical arena**, mapped via a single
  **shared L1 subtree**. Installing that one L2 entry into a page-table
  root exposes the whole window — done for the boot PT in
  `Croi_Shared_Init` and for every task PT in the `sched.c` spawn paths
  (`Croi_Shared_InstallMapping`). `Croi_DestroyPT` skips `L2[4]`.
- **`sstatus.SUM=1`** is set in `_start.S` (line ~58). This is the
  load-bearing fact: a single lower-half SASOS pointer is dereferenceable
  by **both** the kernel (S-mode) and U-mode. Don't remove SUM.
- `Croi_AllocShared(size)` allocates from a slab `Heap` over the arena
  (`Heap_InitArena` gives it the fixed phys↔VA window offset).
  `Croi_Free(ptr)` **range-routes**: a pointer in the shared window goes
  to the shared heap, everything else to the kernel heap. So a single
  `Croi_Free` works for both.
- **`AllocMem` (exec.library) now allocates from the shared heap** (S2),
  so library pointers are U-dereferenceable. Internal kernel code still
  uses `Croi_Alloc` (upper-half kernel heap). **Leargas
  Screen/Window/IntuiMessage allocate shared** (S3).
- **Proven end-to-end**: `userexec` (a real U-mode Gleas) `AllocMem`s,
  writes, and reads back shared memory — `KERNEL_TEST(userexec_smoke)`
  passing means U-mode SASOS access works.
- Phase-1 limitations (fine for now, noted for later): fixed 8 MiB arena
  (growable via more L2 slots / lazy mapping); no per-owner isolation —
  the window is globally RW+U (acceptable under the "total trust" model);
  `KERNEL_TEST(shared_heap)` covers it.

### 2.2 Leargas (Subgoal 6, complete)

Kernel-side window-system substrate, all in `src/croi/leargas/`, driven
today from the boot path + HID poll in `entry.c`. The brand-namespace
`Leargas_*` API is what `intuition.library` LVO bodies will bridge onto
(see `docs/PHASE1_LEARGAS.md` for the full L0–LH writeup):

- Pointer, screen, window primitives + decoration render.
- **LE** focus/activation (`Leargas_ActiveWindow`/`SetActiveWindow`,
  hit-test).
- **LF** keyboard IDCMP routing: per-window `UserPort` (a
  `KOBJ_MSGPORT`), `struct IntuiMessage`, RAWKEY → focused window via a
  router hook (`Leargas_SetKeyRouter` → `Leargas_IDCMP_RouteKey`).
- **LG** gadgets: `struct Gadget`, `AddGadget`/`RemoveGadget`, hit-test,
  render, press/select in the router.
- **LH** string Inntin: `struct StringInfo`, a built-in US keymap
  (`Leargas_RawkeyToAscii`), editing, render w/ cursor, Return →
  `IDCMP_GADGETUP` via `Leargas_SetGadgetRouter`.

---

## 3. The intuition.library bridge — plan & decisions

Goal: let a U-mode Clar call `OpenWindow`/`CloseWindow`/`AddGadget`/
`RemoveGadget`/`ActivateGadget` (canonical V36+ LVOs) that bridge to the
`Leargas_*` substrate. Mirrors the existing `exec.library` machinery.

**Key design decisions already made (don't relitigate):**

1. **Clar runs on the boot-opened Workbench screen** — it does NOT call
   `OpenScreen`. So no `NewScreen`/`OpenScreen` LVO needed; windows open
   with `NewWindow.Screen = nullptr` → Leargas active screen.
2. **Reuse the user-RX trampoline region** — add a `.lib_text.intuition`
   input section to the existing `.exec_lib` output section in
   `src/croi/croi.lds` (it's already mapped `RX+U` into every task PT).
   **Do not** create a second linker library region / image module.
3. **The intuition base + vec table live in the SASOS shared heap.**
   Unlike exec (fixed VA `0x4000_0800`, bootstrapped by libcara),
   `IntuitionBase` is discovered at runtime via
   `OpenLibrary("intuition.library")`. Allocate the
   `lib_NegSize + lib_PosSize` block with `Croi_AllocShared`; since
   `SUM=1`, the kernel writes it directly (so `MKL_BASE` and
   `MKL_BASE_KERNEL_WRITE` are the same shared VA — no separate kernel
   view as exec needs).

**Done — I1 (`25270ad`):**
- `tools/lvo-gen/intuition.conf` (reserved + pads + the 5 funcs at
  canonical LVOs; `syscall` flavour → `Cara_Trampoline_<Name>`).
- `include/intuition/intuitionbase.h` (`struct IntuitionBase`).
- `src/croi/intuition_lib/CMakeLists.txt` (gen-only) wired into the
  rv64 build; `croi` depends on `cara_intuition_lib_gen`.
- Generates (in `build-rv64/gen/`): `proto/intuition.h`,
  `intuition/lvo.h`, `src/croi/intuition_lib/intuition_vec.c`,
  `aistreoir/intuition.inc`. `<proto/intuition.h>` compiles standalone.

**Done — I2 (`dfa0f16`, trampolines + dispatch + bodies):**
- `src/croi/intuition_lib/trampolines.S`: section `.lib_text.intuition`,
  one `CARA_SYSCALL_TRAMPOLINE Cara_Trampoline_<Name>, SYS_<Name>` per
  func (copy the macro from `src/croi/exec_lib/trampolines.S`).
- `src/croi/croi.lds`: add `KEEP(*(.lib_text.intuition))` next to the
  existing `KEEP(*(.lib_text.exec))` in the `.exec_lib` section.
- `include/cara/sysno.h`: add `SYS_OpenWindow` … `SYS_ActivateGadget`
  (continue numbering from 15).
- `src/croi/syscall/syscall.c`: dispatcher `case` arms →
  `Croi_<Name>_Impl`.
- `src/croi/intuition_lib/*.c`: the bodies (signature: V36+ args +
  trailing `struct IntuitionBase *`, which `local`/`syscall` bodies
  ignore). Bridges:
  - `OpenWindow(nw, base)` → `Leargas_OpenWindow(nw)`
  - `CloseWindow(w, base)` → `Leargas_CloseWindow(w)`
  - `AddGadget(w, g, pos, base)` → `Leargas_AddGadget(w, g)` (pos: Phase 1
    appends; honor position later) then re-render the gadget
  - `RemoveGadget(w, g, base)` → `Leargas_RemoveGadget(w, g)`
  - `ActivateGadget(g, w, req, base)` → `Leargas_SetActiveGadget(g)` +
    render; return TRUE
  - reserved `Open/Close/Expunge/ExtFunc` → hooks like
    `src/croi/exec_lib/exec_hooks.c`
- `src/croi/intuition_lib/CMakeLists.txt`: build a `cara_intuition_lib`
  static lib from the bodies + trampolines.S + the generated
  `intuition_vec.c` (whole-archive it into `croi` like
  `cara_exec_lib`/`cara_kernel_tests` so the vec/trampolines aren't
  GC'd). Link `cara_intuition_lib` into `croi` (`src/croi/CMakeLists.txt`).

**Done — I3 (`d2e8594`, construct + smoke):**
- At boot in `entry.c` (after exec.library's `Croi_MakeLibrary`): alloc
  the intuition base in the shared heap, `Croi_MakeLibrary(intuition tags)`
  with the generated `intuition_lib_vec[]`, and register so
  `OpenLibrary("intuition.library")` resolves it.
- A U-mode smoke (new `userintuition.elf` or extend `userexec`): set
  `IntuitionBase = OpenLibrary("intuition.library", 0)`, `OpenWindow` a
  window on the active screen, assert non-null, `CloseWindow`. Add a
  `KERNEL_TEST` that spawns it and checks the exit code.

---

## 4. Known gaps that block Clar specifically (resolve during Clar)

These were noted in passing and are NOT yet done — a fresh session must
handle them when wiring Clar:

1. **No boolean-gadget `IDCMP_GADGETUP`.** LG delivers `GADGETUP` only
   for the string Inntin on Return (LH). A "drawer" `BOOLGADGET` clicked
   to open the drawer needs `GACT_RELVERIFY` → `GADGETUP`-on-release
   wired into the router's button-up path (reuse `Leargas_IDCMP_PostGadgetUp`).
2. **Close gadget doesn't post `IDCMP_CLOSEWINDOW`.** The window close
   gadget is drawn as chrome (LD) but not hit-tested/wired. Clar needs a
   click on it → `IDCMP_CLOSEWINDOW` on the window's UserPort to close
   the drawer window.
3. **Boot is one-shot; Clar needs a persistent event loop.** `entry.c`
   today: HID poll (pre-`Sched_Init`) → bring up scheduler → run tests →
   `Croi_Halt()`. A U-mode Clar `WaitPort`s its UserPort, so there must
   be an input-producer path (HID poll → `Leargas_Input_Drain`, which
   posts IDCMP that signals Clar) running concurrently with Clar. Decide
   the model: e.g. an input/poll kernel task + Clar as a U-mode task, or
   restructure the boot loop. This is the main structural piece for Clar.
4. **Clar must `AllocMem` its own gadgets/StringInfo/buffer** (shared
   heap) so the kernel router can dereference them from any task context.

`docs/PHASE1_CLAR.md` has the original (pre-shared-heap) plan; its
"Clar is a U-mode Gleas via intuition LVOs" framing is now the actual
path. Its CA.1 (`.incbin clar.elf`) / CB / CC / CD / CE / CF / CG tiers
still apply, adjusted for the gaps above.

---

## 5. Build / test / commit workflow

**Two build dirs** (`CARA_TARGET` fixed per dir). The rv64 build needs
the host-built `lvo-gen`; build host first, rv64 auto-detects it.

```bash
# Host: tool + portable modules + unit tests
cmake -S . -B build-host
cmake --build build-host -j4
(cd build-host && ctest)                 # 20/20 expected

# RV64 kernel (auto-detects build-host/tools/lvo-gen/lvo-gen)
cmake -S . -B build-rv64 -DCARA_TARGET=riscv64 \
      -DCMAKE_TOOLCHAIN_FILE=cmake/toolchain-riscv64.cmake
cmake --build build-rv64 -j4             # -Werror; produces src/croi/croi.elf
```

**The "green" gate before any commit** (the user's standing rule —
commit automatically at the end of a green epic; see the project memory
`commit-at-green-epic-end`):

```bash
# 1. host unit tests
(cd build-host && ctest)                                   # 20/20, 0 failed
# 2. kernel boot smoke (MUST pass the -device flags; the harness does)
bash tests/boot/smoke_qemu_kernel.sh "$(command -v qemu-system-riscv64)" \
     build-rv64/src/croi/croi.elf                          # "smoke_qemu_kernel: ok"
# 3. format-check (uses Homebrew LLVM 22 clang-format)
cmake --build build-host --target format-check             # PASS
```

Gotchas:
- **Run the kernel WITH USB devices** for an accurate test count:
  `qemu-system-riscv64 -M virt -m 256 -nographic -bios default -kernel
  build-rv64/src/croi/croi.elf -device qemu-xhci -device usb-kbd
  -device usb-mouse`. Without them, the xHCI/PCI tests fail (2 failures)
  — that's expected, not a regression. The smoke harness includes them.
- **clang-format version skew**: the tree was reformatted to Homebrew
  LLVM 22 (`/opt/homebrew/opt/llvm/bin/clang-format`). Format new/edited
  C files with that exact binary. Keep cosmetic churn in a separate
  `style:` commit (see project memory `clang-format-version-skew`).
- The LSP diagnostics in-editor show `'cara/types.h' file not found`
  etc. — that's the standalone LSP missing `-Iinclude`; ignore. The
  CMake build has the include paths.
- Commit style: subject `phase-N/<Epic>: <short>`, bullet body, end with
  `Co-Authored-By: Claude Opus 4.8 (1M context) <noreply@anthropic.com>`.
  Commit to `main` (linear, trunk-based history).
- **Do not commit** `amiga_docs/` (large reference PDFs, incl. the
  Internet-Archive devcon PDF) or `CLAUDE.md` unless asked.

---

## 6. Quick orientation map

- `docs/ARCHITECTURE.md` — the design (SASOS §4, Kobj/Handles §5, IPC
  §6, libraries/LVO §7). `docs/LVO.md` — the library-bridge model.
- `include/cara/shared.h`, `src/croi/mm/shared.c` — SASOS shared heap.
- `src/croi/leargas/` — the window system (Subgoal 6).
- `src/croi/exec_lib/` + `tools/lvo-gen/exec.conf` — the reference the
  intuition bridge mirrors (`trampolines.S`, `make_library.c`,
  `image.c`, `exec_hooks.c`, `mem.c`).
- `src/croi/intuition_lib/` + `tools/lvo-gen/intuition.conf` — the
  in-progress intuition bridge.
- `src/croi/syscall/syscall.c` — the `a7` dispatcher (add arms here).
- `include/cara/sysno.h` — syscall numbers (add intuition's here).
- `src/croi/entry.c` — boot path (MakeLibrary calls, Leargas demo,
  where Clar gets spawned).
- `src/croi/croi.lds` — linker layout (the `.exec_lib` region to extend).
- `src/croi/tests/` — in-kernel `KERNEL_TEST()`s; `tests/unit/` — host
  unit tests; `tests/boot/smoke_qemu_kernel.sh` — the boot gate.
