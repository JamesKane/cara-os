# CaraOS session handoff — 2026-06-28

> A pick-up-where-we-left-off note for a fresh session: current state, the
> active work, the build/test/commit workflow, and the conventions that aren't
> obvious from one file. Per-slice history lives in `git log` (every slice is a
> commit with a full message); per-epic design lives in the docs cited below.
> Pairs with `docs/ARCHITECTURE.md` (system design), `docs/PRINCIPLES.md` (the
> rules), `docs/LVO.md` (library dispatch), and the epic docs in `docs/`.

---

## 1. Where we are

CaraOS is a cleanroom C23 reimplementation of the AmigaOS Release 2 (V36+) spec,
a single-address-space OS. It has grown through:

- **Phase 1 — boot to a usable machine (DONE).** Boot -> FDT -> mm -> scheduler
  -> IPC -> framebuffer + USB HID (pointer + keyboard), under QEMU `virt`.
- **Phase 2 — CaraFS + persistence (DONE under QEMU).** A journaling filesystem
  on an NVMe-resident GPT partition; edits persist across reboot; the on-disk
  format is frozen. (`docs/CARAFS.md`, `docs/LOGAIC_BOOT.md`.)
- **Phase 3 — the V36+ API surface, L1-L14 (DONE).** exec / utility / dos
  (an AmigaDOS handler) / graphics / intuition / devices / BOOPSI / gadtools /
  asl / iffparse / icon / diskfont / commodities / expansion — verbatim V36+
  names, generated dispatch (`docs/PHASE3.md`, `docs/LVO.md`).
- **Phase T — port a real app (DONE).** Boot -> Workbench + console Shell; type
  `amicalc` -> a real third-party MIT GUI calculator runs **unedited**.
  (`docs/PORTS.md`.)

**Active epic: H — the arch HAL + an ARM64 backend** (`docs/ARCH_HAL.md`,
`docs/ARM64.md`). See section 3. The motivating goal — prove the arch boundary
with a second ISA — is met; the current tail is bringing the rest of the
portable kernel up on AArch64.

Status: **green.** Host `ctest` 32/32; the rv64 two-boot QEMU smoke ok
(in-kernel runner `0 failed`); the arm64 boot smoke ok; `format-check` clean.

---

## 2. Build / test / commit workflow

Three build directories, each pinned to a target/arch (never flipped in place):

```bash
# host — unit tests + the lvo-gen tool + portable modules (Apple Clang ok)
cmake -S . -B build-host
cmake --build build-host
ctest --test-dir build-host --output-on-failure

# riscv64 kernel — the primary, fully-built kernel (Homebrew LLVM 22+)
cmake -S . -B build-rv64 -DCARA_TARGET=riscv64 \
      -DCMAKE_TOOLCHAIN_FILE=cmake/toolchain-riscv64.cmake
cmake --build build-rv64               # -> build-rv64/src/croi/croi.elf

# arm64 kernel — the second backend (epic H.7), grows slice by slice
cmake -S . -B build-arm64 -DCARA_TARGET=riscv64 -DCARA_ARCH=arm64 \
      -DCMAKE_TOOLCHAIN_FILE=cmake/toolchain-arm64.cmake
cmake --build build-arm64               # -> build-arm64/src/croi/croi.elf
```

(`CARA_TARGET=riscv64` means "cross-build the kernel"; `CARA_ARCH` names the ISA.
Folding the two is a later cleanup.)

Boot smokes (wire build dirs into the host build so `ctest` runs them):

```bash
cmake -S . -B build-host \
      -DCARA_RV64_BUILD_DIR=$PWD/build-rv64 \
      -DCARA_ARM64_BUILD_DIR=$PWD/build-arm64
ctest --test-dir build-host -R smoke_qemu          # both arches
# interactive: cmake --build build-host --target qemu-virt-kernel        (rv64)
#              cmake --build build-host --target qemu-virt-kernel-arm64   (arm64)
```

**Formatting:** `cmake --build build-host --target format-check` (CI-style) /
`format` (rewrite). The tree was formatted by an older clang-format than local
LLVM 22 — format only files you touched, in their own commit, to avoid churn.
Asm-macro include fragments use `.inc` (not `.h`) so they dodge the `*.h` glob.

**Per-slice gate (every commit):** rv64 builds + two-boot smoke `ok` + in-kernel
runner `0 failed`; arm64 builds + boots as far as the slice reaches; host
`ctest`; `format-check` clean. Then commit to `main` (co-author trailer
`Co-Authored-By: Claude Opus 4.8 (1M context) <noreply@anthropic.com>`), then a
follow-up commit updating this doc + the session memory. Regenerate
`docs/LVO_COVERAGE.md` only if a slice touched a `.conf`. Never commit
`CLAUDE.md` or `amiga_docs/`.

---

## 3. The active work: epic H — arch HAL + ARM64

**Goal:** carve the RISC-V-specific kernel internals behind a small `arch_*`
boundary (`include/cara/arch.h` + `include/cara/arch_pte.h`), then add an ARM64
(AArch64) backend — proving the boundary with a real second ISA. One ISA per
build image, selected at configure time by `CARA_ARCH` (no runtime dispatch).

**H.1-H.6 — the carve-out (DONE).** Every RISC-V internal now lives behind
`cara/arch.h` in `src/croi/arch/riscv64/` (boot, trap, ctx switch + FP, MMU,
timer, firmware), selected by `CARA_ARCH`; RISC-V stayed bit-for-bit green
throughout. (`docs/ARCH_HAL.md`.)

**H.7 — the ARM64 backend** (`src/croi/arch/arm64/`, `docs/ARM64.md`). The HAL
is fully implemented and validated on AArch64, and the portable core kernel is
coming up on top of it:

| Slice  | What                                                | Status |
|--------|-----------------------------------------------------|--------|
| H.7.1  | toolchain + boot-to-print (EL1, PL011)              | done   |
| H.7.2  | stage-1 paging -> SASOS upper half                  | done   |
| H.7.2b | cara_mm/cara_fdt runtime + arch_pte.h split         | done   |
| H.7.3  | trap + syscall (VBAR_EL1, svc, TrapFrame)           | done   |
| H.7.4  | per-task page tables, ctx switch, NEON FP, EL1<->EL0 | done  |
| H.7.5  | generic timer + GICv2 IRQ + PSCI power-off          | done   |
| H.7.6  | factor the syscall-trampoline macro (svc)           | done   |
| H.7.7a | portable cara_time                                  | done   |
| H.7.7b | cara_log (PL011 sink)                               | done   |
| H.7.7c | the SASOS shared heap (shared.c)                    | done   |
| H.7.7d | cara_sched + real kernel tasks                      | done   |
| H.7.7e | first real U-mode program (EL0)                     | done   |
| H.7.7f | library surface + real cara_syscall + parity        | NEXT   |

The arm64 kernel boots through the whole HAL + core kernel end to end:
**paging -> FDT+mm -> traps -> per-task PT -> ctx/FP -> EL0 -> timer/IRQ ->
PSCI**, plus cara_time / cara_log / the shared heap / the cooperative scheduler /
a real U-mode program — all driven from `arch/arm64/boot.c` (a bring-up harness
that a real `croi_entry` will eventually supersede), validated by
`smoke_qemu_arm64`.

### What's next — H.7.7f (a large multi-slice tail)

1. **`cara_exec_lib_image`** — the `0x4000_0000` RX library region + the
   lvo-gen'd `exec_lib_vec` + trampolines (replacing the temporary
   `arch/arm64/exec_lib_stub.c`). The arm64 `kernel.lds` has no library region
   yet — it needs one (the rv64 `croi.lds` has a `LIB_VIRT` window).
2. **The real `cara_syscall`** — its dispatch table references every library
   impl, so linking it cascades into all 14 V36+ libraries + `cara_ipc` +
   devices. Bring them in (each: `add_subdirectory` + link + lvo-gen). Then
   delete the boot.c minimal `Croi_Syscall_Dispatch` + `trampoline_demo.S`.
3. **A real app + an arm64-built userland** (`src/userland` is RISC-V-built
   today; needs an arm64 build or per-arch ELFs).
4. **Parity** — an arm64 in-kernel test runner (the `.kernel_tests` registry +
   `runner.c`) so the arm64 smoke can assert `0 failed`; fold the arm64
   `kernel.lds` into a `CARA_ARCH`-selected linker script.

### Temporaries on the arm64 side (delete when the real thing lands)

- `arch/arm64/exec_lib_stub.c` — `Croi_ExecLib_InstallMapping` -> `cara_exec_lib_image`.
- `arch/arm64/trampoline_demo.S` + the minimal `Croi_Syscall_Dispatch` in
  `boot.c` — -> the real `cara_syscall`.
- The demos in `boot.c` (ctx/fp/timer/shared/sched/U-mode) — -> a real `croi_entry`.

### AArch64 specifics worth remembering

- **TTBR0/TTBR1 split**, not one `satp`. The kernel half is the fixed TTBR1
  (set in `_start.S`); per-task page tables are the TTBR0 root only
  (`Croi_NewKernelPT` is arch-gated — empty root on arm64). `arch_mmu_activate`
  swaps TTBR0 and leaves the kernel running. The PL011 console + GIC are reached
  via the TTBR1 alias so they survive TTBR0 switches.
- **The generic page-table walk is unchanged** — it only queries
  `arch_pte_is_leaf` at upper levels (block detection), where per-task entries
  are always tables, so the bits-only test is correct (note in
  `cara/arch/arm64/arch_pte.h`).
- **IRQs are vectored, not cause-unified.** `trap_entry.S` tags the frame with a
  `kind`; the common path routes IRQ -> `arm64_irq_dispatch` (GIC), sync ->
  the portable `Croi_TrapDispatch`. `svc` leaves `ELR` already past itself, so
  `arch_trap_advance` is a no-op.
- **QEMU virt quirks:** must name a 64-bit CPU (`-cpu cortex-a72`); the ELF
  `-kernel` path leaves `x0=0` (no DTB), so the kernel finds the DTB at RAM base.
  PSCI `SYSTEM_OFF` exits QEMU cleanly (the smoke finishes in ~1s, no timeout).
- **`-mgeneral-regs-only`** kernel codegen; FP/SIMD asm in `.S` needs
  `.arch armv8-a`. `PTE_USER_*` composed masks are the portable prot contract
  (raw Sv39 bits don't exist on AArch64).

---

## 4. Architecture you can't infer from one file

The three load-bearing rules (full text in `docs/PRINCIPLES.md` / `ARCHITECTURE.md`):

- **Brand-vs-API namespace split (PRINCIPLES 3.1).** `include/exec/*`,
  `include/intuition/*`, ... are **verbatim AmigaOS V36+** (exact names, structs,
  offsets — the contract that lets V36 source build unedited). `include/cara/*`
  and `src/<brand>/` are kernel-internal (Irish brand names: Croi=kernel,
  Leargas=intuition, Dath=graphics, Logaic=dos, ...). The namespaces meet only
  at the LVO trampoline.
- **LVO dispatch (`docs/LVO.md`).** Library APIs are **generated, not
  hand-written**: `tools/lvo-gen/<lib>.conf` is the source of truth; lvo-gen
  emits the proto stubs, vec table, and per-LVO trampolines. Each LVO has a
  flavour — `local` (in-process call), `syscall` (`ecall`/`svc`), or `server`
  (PutMsg round-trip).
- **SASOS + Handles (ARCHITECTURE 4-6).** One Sv39/Stage-1 address space; the
  MMU is for fault containment, not security. Public AmigaOS calls return
  canonical pointers valid in any task; the kernel uses generation-checked
  Handles internally. IPC is a lock-free SPSC ring in shared memory (zero-copy).

---

## 5. Recent commits (newest first; `git log` for the rest)

```
epic-H/H.7.7e  ARM64 — first real U-mode program (EL0)
epic-H/H.7.7d  ARM64 — link cara_sched; real kernel tasks
epic-H/H.7.7c  ARM64 — SASOS shared heap (shared.c)
epic-H/H.7.7b  ARM64 — cara_log (structured logging)
epic-H/H.7.7a  ARM64 — portable cara_time layer
epic-H/H.7.6   ARM64 — factor the syscall-trampoline macro (svc)
epic-H/H.7.5   ARM64 — timer + GIC IRQ + PSCI
epic-H/H.7.4   ARM64 — per-task PT + ctx switch + FP + enter-U-mode (a-d)
epic-H/H.7.3   ARM64 — trap + syscall seam (VBAR + svc)
epic-H/H.7.2   ARM64 — stage-1 paging + cara_mm runtime + arch_pte.h split
epic-H/H.7.1   ARM64 — toolchain + boot-to-print
epic-H/H.1-H.6 arch HAL carve-out (riscv64 behind cara/arch.h)
phase-T        boot -> Workbench + Shell; amiCalc (MIT GUI) runs unedited
phase-3/L1-L14 the V36+ library surface (exec ... expansion)
phase-2        CaraFS + GPT + NVMe + reboot persistence
phase-1        boot -> mm -> sched -> IPC -> framebuffer + USB HID
```
