<!-- SPDX-License-Identifier: BSD-2-Clause -->
# Arch HAL refactor — scope (epic H)

> Carve the RISC-V-specific kernel internals behind a small, named arch
> boundary (`arch/`) so a second architecture — **ARM64 (AArch64)** — can be
> added without touching the portable kernel. The near-term body of the epic
> is a **behaviour-preserving** refactor: RISC-V stays bit-for-bit the same
> and green after every slice; the ARM64 backend is the terminal goal.
>
> Drivers: the `arch-hal-roadmap` note (RV → ARM64, HAL is the fast-follow).
> This epic post-dates the current `docs/ROADMAP.md` (which ends at apps); it
> is structural, not feature work.

## 1. Goal / non-goals

**Goal.** A portable Croi kernel that calls a fixed set of `arch_*`
operations, with each architecture supplying them under `src/croi/arch/<arch>/`.
One architecture per build image (like `CARA_TARGET`), selected at configure
time by a new `CARA_ARCH` variable. Adding ARM64 becomes "write
`arch/arm64/` + a toolchain file", not "edit the scheduler / mm / trap core".

**Non-goals (this epic's carve-out body, H.1–H.6).**
- No behaviour change on RISC-V. Every carve-out slice ends green on rv64
  (two-boot QEMU smoke `ok`, in-kernel runner `0 failed`, host ctest, format).
- No new features, no SMP work, no Sv48 — just relocate + name the seam.
- ARM64 itself (H.7+) is a large sub-effort with its own toolchain + QEMU
  target; it is scoped here but lands after the carve-out.

**Honesty about validation.** A boundary is only *proven* right when a second
arch uses it. The carve-out (H.1–H.6) organises the code and documents the
seams; expect the interface to be *refined* when ARM64 (H.7) actually exercises
it. That is acceptable — the carve keeps RISC-V green throughout and turns the
ARM64 effort into filling in `arch/arm64/`.

## 2. Mechanism: compile-time, one arch per build

No runtime vtable. The portable kernel calls `extern` `arch_*` functions
declared in `include/cara/arch.h`; the build links exactly one
`src/croi/arch/<arch>/` backend. This matches the existing `CARA_TARGET`
two-build-dir philosophy (`docs/PRINCIPLES.md`): the arch is fixed per build
directory, never switched at runtime, so there is zero dispatch cost and the
linker catches a missing seam.

```
CARA_ARCH = riscv64   (default; the only backend today)
            arm64     (added in H.7)
```

`CARA_TARGET=riscv64` keeps meaning "cross-build the kernel"; `CARA_ARCH`
names *which* ISA. (A later cleanup may fold them, but keeping them distinct
avoids churn now.)

## 3. The HAL boundary (`include/cara/arch.h`)

The ~12 seams the inventory found, grouped. Each is a thin `arch_*` surface;
everything else in `src/croi/` stays portable C. (Names are indicative; the
exact signatures are settled per slice.)

| Seam | Portable caller(s) | RISC-V today | ARM64 (H.7) |
|------|--------------------|--------------|-------------|
| **Boot bring-up** | `_start` → `croi_entry(hartid, dtb)` | `_start.S`: hartid in tp, sstatus SUM+FS, boot Sv39 PT, satp, jump to upper half | EL1 entry, `x0=dtb`, MAIR/TCR/TTBR1, MMU enable |
| **Trap entry/exit** | `croi_trap_entry` (asm) → `Croi_TrapDispatch` | `trap_entry.S` (sscratch swap, save x1–x31 + sepc/scause/stval/sstatus), `sret` | `VBAR_EL1` vectors, save x0–x30/SP/ELR/SPSR/ESR, `eret` |
| **Trap classify** | `Croi_TrapDispatch` | scause: intr-bit, cause 8 = ecall, 5 = timer | ESR_EL1.EC: SVC, IRQ, aborts |
| **Syscall ABI** | `Croi_Syscall_Dispatch(frame)` | a7 = number, a0–a5 args, a0 = ret; `ecall`; sepc += 4 | x8 = number, x0–x5 args, x0 = ret; `svc #0`; ELR unchanged |
| **Context switch** | `Croi_Yield/Wait/TaskExit` | `ctx_switch.S`: ra/sp/gp/tp/s0–s11 + sscratch | x19–x30/sp + tpidr |
| **FP context** | (inside ctx switch) | f0–f31 + fcsr (`fld`/`fsd`), `sstatus.FS` | v0–v31 + fpsr/fpcr, `CPACR_EL1` |
| **Enter U-mode** | `user_task_trampoline` | set satp/sstatus(SPP=0,SPIE,SUM,FS)/sepc/sscratch, clear GPRs, `sret` | TTBR0/SPSR_EL1(EL0)/ELR_EL1, `eret` |
| **MMU map** | `Page_Map` (Sv39 walk) | PTE bits V/R/W/X/U/A/D, PPN shift | AArch64 stage-1 descriptors (AF/AP/UXN/PXN/nG) |
| **MMU activate / fence** | `sched_activate_as`, user trampoline | `csrw satp` + `sfence.vma` | `TTBR0_EL1` + `TLBI` + `DSB`/`ISB` |
| **VA layout** | `mm.h` constants | `KERNEL_VA_OFFSET`, upper/lower-half, PTE flag macros | TTBR1 (high) / TTBR0 (low) — maps to the same split |
| **Timer** | `Croi_Time_*` | `rdtime`/`stimecmp` (Sstc), SIE.STIE | `CNTVCT_EL0`/`CNTV_CVAL_EL0`/`CNTV_CTL_EL0` |
| **IRQ mask / halt / firmware** | `time.c`, `panic.c`, `sbi.c` | `sstatus.SIE`, `wfi`, SBI console/PSCI | DAIF, `wfi`, PSCI |

**Stays portable, untouched** (already arch-neutral C, compiles host + kernel):
FDT parser, page/heap allocators + physmap, the Sv39 *walk* logic (the PTE
*encoding* is the arch part), scheduler policy, IPC ring/msgport, CaraFS, GPT,
HID decode, Dath graphics, Leargas, all lvo-gen'd library bodies + impls, the
syscall *dispatch table* (`syscall.c` switch).

## 4. Hard problems to resolve in design (not just code-move)

These are the places where ARM64 is genuinely *different*, not just renamed —
the carve-out must leave a seam shaped to absorb them:

1. **Page-table encoding.** Sv39 PTEs (V/R/W/X/U/A/D, PPN at [53:10]) vs
   AArch64 stage-1 descriptors (valid+table/page bits, AP[2:1], AF, UXN/PXN,
   nG, attr-index). The generic *walk* (3-level, 4 KiB pages, 9-bit indices)
   is common to Sv39 and AArch64-4K-granule, so the walker can stay portable
   if PTE compose/decode + the "activate" move to arch. **The SASOS VA layout
   (upper/lower half, the `0x4000_0000` library region, the heap/slab/IPC
   windows in ARCHITECTURE.md §4.3) is preserved** — ARM64's TTBR1/TTBR0 split
   is a natural fit for kernel-high / user-low.

2. **Trap model.** RISC-V has one `stvec` and a cause register; AArch64 has a
   16-entry `VBAR_EL1` table and decodes `ESR_EL1.EC`. The portable dispatcher
   must see a normalised `struct TrapFrame` + arch accessors (`is_syscall`,
   `syscall_args`, `advance_pc`) rather than raw `scause`.

3. **Syscall trampolines are arch-specific *generated* code.** `lvo-gen` emits
   `Cara_Trampoline_<Name>` stubs (`li a7,N; ecall; ret`) into the `.lib_text`
   RX region. ARM64 needs `mov x8,N; svc #0; ret`. So **`lvo-gen`'s trampoline
   emitter becomes arch-aware** (a per-arch template), and the hand-written
   `*/trampolines.S` (dos, intuition, exec, …) likewise. This is the one place
   the arch reaches *up* into the build tooling — flag it early.

4. **FP register file.** f0–f31+fcsr vs v0–v31+fpsr/fpcr; enable bit
   `sstatus.FS` vs `CPACR_EL1.FPEN`. The `Task.fp_save` area + save/restore
   move wholesale to arch.

5. **Firmware / boot ABI.** SBI (timer, console, IPI, HSM) vs PSCI + generic
   timer + a chosen early console (PL011 on QEMU virt-arm). Boot args:
   `a0=hartid,a1=dtb` vs `x0=dtb`.

## 5. Slice plan

Behaviour-preserving carve-out, cheapest/most-isolated seams first so the
`arch/` structure + pattern land before touching the boot/trap core:

- **H.1 — arch skeleton + leaf seams.** Create `include/cara/arch.h` +
  `src/croi/arch/riscv64/`. Move the self-contained ops: `arch_halt` (wfi),
  `arch_irq_enable/disable` (sstatus.SIE), the timer CSR layer (`time.c` →
  `arch/riscv64/timer.c` behind `arch_timer_now/set_deadline/...`), and the
  firmware/early console (`sbi.c` → `arch/riscv64/firmware.c`). Portable
  callers updated; rv64 green. (Warm-up: establishes the dir + CMake wiring.)
- **H.2 — MMU seam.** PTE compose/decode + `Sv39_Satp` + activate/fence +
  boot-leaf install + `shared.c` CSR bits → `arch/riscv64/mmu.c`; keep the
  generic walk portable, parameterised by arch PTE ops. VA-layout constants
  reviewed for arch-neutrality.
- **H.3 — context switch + FP.** `ctx_switch.S` + `user_trampoline.c` + the FP
  file → `arch/riscv64/`; portable scheduler calls `arch_ctx_switch` +
  `arch_enter_user`.
- **H.4 — trap + syscall seam.** `trap_entry.S` + cause-decode + the
  `TrapFrame` + arg extraction → `arch/riscv64/`; the dispatcher uses arch
  accessors. `stvec` install behind `arch_trap_init`.
- **H.5 — boot seam.** `_start.S` + the boot bring-up half of `croi_entry` →
  `arch/riscv64/boot`; boot ABI behind the arch.
- **H.6 — build + tooling + docs.** Introduce `CARA_ARCH` (default riscv64),
  make CMake select the backend, make `lvo-gen`'s trampoline emitter
  arch-aware (template per arch), and generalise `ARCHITECTURE.md §4` +
  `PRINCIPLES.md` (the "stock RISC-V only" stance → "primary RISC-V; arch/ HAL;
  ARM64 second target"). rv64 still the only built arch.
- **H.7+ — ARM64 backend (terminal goal, its own sub-epic).**
  `cmake/toolchain-arm64.cmake`, `src/croi/arch/arm64/` (EL1 boot, VBAR
  vectors + eret, svc syscall, TTBR0/1 4K paging, CNTVCT timer, PSCI, NEON FP),
  `svc` trampoline template, QEMU `-M virt` (AArch64) boot smoke. Sliced
  separately once the seams are proven.

## 6. Risks / watch-items

- **Over-abstraction.** Keep `arch.h` minimal — only what a real second arch
  needs differently. Resist speculative generality; the carve documents what
  *is* arch-specific, nothing more.
- **VA-layout assumptions** baked into headers + lvo-gen (the `0x4000_0000`
  region, `KERNEL_VA_OFFSET`). Audit in H.2/H.6; keep the layout identical.
- **The lvo-gen trampoline coupling** (§4.3) — the deepest cross-cutting item;
  do not let H.6 surprise us, note it in H.1.
- **Carve-without-consumer** designing the wrong seam — mitigated by keeping
  RISC-V green and accepting refinement at H.7.

## 7. Per-slice gate

Same as every epic: each slice ends green — rv64 builds, two-boot QEMU smoke
`ok`, in-kernel runner `0 failed`, host `ctest`, `format-check` clean —
committed to `main`, then a follow-up commit updates `docs/HANDOFF.md` + the
session memory. `docs/LVO_COVERAGE.md` is regenerated only if a slice touches a
`.conf` (the carve-out should not). Never commit `CLAUDE.md` / `amiga_docs/`.
