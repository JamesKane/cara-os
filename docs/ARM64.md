<!-- SPDX-License-Identifier: BSD-2-Clause -->
# ARM64 (AArch64) backend — scope (epic H, H.7)

> Fill in the second `arch/` backend behind the HAL that epic H.1–H.6 carved
> out (`docs/ARCH_HAL.md`). This is where the boundary in `include/cara/arch.h`
> gets *validated* by a real second ISA — expect the interface to be refined as
> AArch64 exercises seams that RISC-V settled with different shapes.
>
> Driver: the `arch-hal-roadmap` note (RV2 → a stock ARM64 SoC). Terminal goal
> of epic H.

## 1. Goal / non-goals

**Goal.** A `src/croi/arch/arm64/` backend supplying the same `arch_*` surface
the portable kernel already calls, selected by `CARA_ARCH=arm64`, booting under
`qemu-system-aarch64 -M virt` to the same place the RISC-V kernel reaches:
the in-kernel test runner reporting `0 failed`, then (eventually) the Shell.
Adding ARM64 must not regress RISC-V — `riscv64` stays the default and bit-for-bit
green after every slice.

**Non-goals.** No real ARM64 hardware bring-up (QEMU-first, like RISC-V — a
board is a later concern). No SMP (single core, like the RISC-V kernel today).
No 16 KiB / 64 KiB granule (we use the 4 KiB granule so the generic 3-level
9-bit-index page-table *walk* stays shared with Sv39). No Sv48-equivalent 4-level
walk yet.

## 2. Mechanism — same as the carve-out

One ISA per build image, chosen at configure time. The RISC-V kernel is built
with `CARA_TARGET=riscv64` (default `CARA_ARCH=riscv64`); the ARM64 kernel with

```bash
cmake -S . -B build-arm64 -DCARA_TARGET=riscv64 -DCARA_ARCH=arm64 \
      -DCMAKE_TOOLCHAIN_FILE=cmake/toolchain-arm64.cmake
cmake --build build-arm64
```

`CARA_TARGET=riscv64` keeps its carve-out meaning — "cross-build the kernel";
`CARA_ARCH` names *which* ISA. (Folding the two is a later cleanup; keeping them
distinct avoids churning the host/rv64 builds now.) A third build dir
(`build-arm64`) sits beside `build-host` + `build-rv64`.

## 3. Seam-by-seam: RISC-V → AArch64

The `arch_*` surface (`include/cara/arch.h`) and what each backend does:

| Seam | RISC-V (`arch/riscv64/`) | AArch64 (`arch/arm64/`) |
|------|--------------------------|-------------------------|
| **Boot** | `_start.S`: OpenSBI hands off S-mode, `a0=hartid a1=dtb`; build Sv39 boot PT, `satp`, jump high | `_start.S`: QEMU hands off EL1 (drop from EL2 if needed), `x0=dtb`; build stage-1 boot PT, `TTBR0/1_EL1`+`SCTLR_EL1.M`, jump high |
| **Early console** | SBI DBCN / legacy putchar (`firmware.c`) | PL011 UART MMIO at the QEMU-virt early address (`firmware.c`); the FDT-driven driver supersedes it, exactly as NS16550 supersedes SBI |
| **CPU control** | `wfi`, `sstatus.SIE` (`cpu.c`) | `wfi`, `DAIF` (`cpu.c`) |
| **Timer** | `time`/`stimecmp` CSRs, `sie.STIE` (`timer.c`) | `CNTVCT_EL0`/`CNTV_CVAL_EL0`/`CNTV_CTL_EL0`, GIC PPI (`timer.c`) |
| **MMU** | `satp` + `sfence.vma`; Sv39 PTE bits (`mmu.c`, `arch_pte.h`) | `TTBR0/1_EL1` + `TLBI`/`DSB`/`ISB`; AArch64 stage-1 descriptors (`mmu.c`, an arm64 `arch_pte.h`) |
| **Trap** | `stvec`, `scause` decode, `trap_entry.S` (`trap.c`) | `VBAR_EL1` 16-entry vectors, `ESR_EL1.EC` decode, `trap_entry.S` (`trap.c`) |
| **Syscall ABI** | `a7`=num, `a0..a5`, `a0`=ret; `ecall`; `sepc+=4` | `x8`=num, `x0..x5`, `x0`=ret; `svc #0`; `ELR` unchanged |
| **Ctx switch** | ra/sp/gp/tp/s0–s11 + `sscratch` (`ctx_switch.S`) | x19–x30/sp + `tpidr_el1`/`sp_el0` (`ctx_switch.S`) |
| **FP ctx** | f0–f31 + fcsr, `sstatus.FS` | v0–v31 + fpcr/fpsr, `CPACR_EL1.FPEN` |
| **Enter U-mode** | set satp/sstatus(SPP=0)/sepc; `sret` (`context.c`) | set TTBR0/SPSR_EL1(EL0t)/ELR_EL1; `eret` (`context.c`) |
| **Halt / firmware** | `wfi`; SBI HSM (later) | `wfi`; PSCI `SYSTEM_OFF`/`CPU_OFF` (later) |

**Shared, untouched** (already proven on RISC-V; H.7 confirms they compile for
AArch64): the page-table *walk* (3-level, 4 KiB, 9-bit indices is common to
Sv39 and AArch64-4K), the scheduler policy, IPC ring, CaraFS, GPT, FDT parser,
HID decode, Dath, Leargas, the lvo-gen'd library bodies, and the syscall
*dispatch table*.

## 4. Things genuinely different (design watch-items)

1. **VA layout maps cleanly.** Our SASOS split (kernel upper-half at
   `KERNEL_VA_OFFSET=0xFFFFFFC0_00000000`, user/lib low-half, the `0x4000_0000`
   library region) is a natural fit for AArch64 `TTBR1_EL1` (high) / `TTBR0_EL1`
   (low). `TCR_EL1.T0SZ/T1SZ=25` gives a 39-bit VA each half (3-level, 4 KiB) —
   the Sv39 equivalent. The constants in `mm.h` are reused; only the descriptor
   *encoding* changes (`arch_pte.h` becomes `CARA_ARCH`-selected in H.7.2).

2. **`arch_pte.h` must become per-arch.** Today `include/cara/arch_pte.h` is
   Sv39-shaped and `mm.h` includes it. H.7.2 makes the include `CARA_ARCH`-
   selected (an `arch/arm64/` variant with AArch64 AP/AF/UXN/PXN/nG/attr-index
   encoding) while the walk stays generic. It must compile on the *host* too
   (cara_mm builds for host unit tests), so it stays pure inline bit math.

3. **Trap model is a vector table, not a cause CSR.** AArch64 has a 16-entry
   `VBAR_EL1` (4 exception types × 4 source states). The portable dispatcher
   only sees a normalised `struct TrapFrame` + the `arch_trap_*` accessors, so
   the entry asm collapses the relevant vectors into one save path and decodes
   `ESR_EL1.EC` in `arch_trap_is_syscall`/`is_timer`.

4. **`struct TrapFrame` is arch-shaped.** It is already documented as such in
   `cara/trap.h`. The AArch64 frame holds x0–x30, SP_EL0, ELR_EL1, SPSR_EL1,
   ESR_EL1; the accessors hide the shape from portable code.

5. **Syscall trampolines (the H.6 deferral).** The per-LVO trampolines are
   hand-written `.S` via the `CARA_SYSCALL_TRAMPOLINE` macro (`li a7,N; ecall;
   ret`). AArch64 needs `mov x8,#N; svc #0; ret`. H.7 factors that macro into a
   `CARA_ARCH`-selected arch include so the per-library `trampolines.S` stay
   arch-neutral. Only bites once U-mode library calls run on ARM64 (H.7.6).

6. **Boot/firmware.** No SBI. QEMU enters at EL1 (drop from EL2 if started
   there); timer/IRQ go through the generic timer + GICv2/v3; power via PSCI.
   The early console is a PL011 at the QEMU-virt address until the FDT-driven
   UART driver installs — the direct analogue of leaning on SBI pre-NS16550.

## 5. Slice plan

Bring-up grows the ARM64 image outward from boot; RISC-V stays green throughout.
Until a slice proves the portable kernel compiles + runs for AArch64, the ARM64
build links only as much as that slice needs (a **minimal `croi` that grows**),
so each slice is small and the gate is real.

- **H.7.1 — toolchain + boot-to-print.** `cmake/toolchain-arm64.cmake`;
  `CARA_ARCH=arm64` accepted; an arm64 *minimal* `croi.elf` = `arch/arm64/`
  (`_start.S` EL1 bring-up, `firmware.c` PL011 console, `cpu.c` halt) + a boot
  stub that prints a banner and halts. Flat low-half link (MMU off). Boots under
  `qemu-system-aarch64 -M virt` and prints the banner (new `smoke_qemu_arm64`).
  *No portable kernel yet.* **(this slice)**
- **H.7.2 — stage-1 paging bring-up (DONE).** Boot stage-1 tables built in
  `_start.S` (two 4 KiB roots, one 1 GiB block descriptor each: TTBR0
  identity-low + TTBR1 upper-half — the AArch64 analogue of the RISC-V boot PT's
  1 GiB Sv39 blocks), `MAIR`/`TCR` (T0SZ=T1SZ=25, 4 KiB granule), enable MMU
  (`SCTLR_EL1.M|C|I`), branch to the upper-half `_high_entry` → C entry running
  at SASOS upper-half VAs. The dual-map linker script (`kernel.lds`) mirrors
  `arch/riscv64/croi.lds`. Proven by the kernel printing its own upper-half code
  address.
  *Re-sequencing note:* the `arch_pte.h` split + `arch_mmu_*` + `cara_mm`
  runtime were folded forward into H.7.2b (below), not done here. AArch64 encodes
  block-vs-page **by level** (block `0b01` at L1/L2, page `0b11` at L3), whereas
  Sv39 (and the current generic walk in `pt.c`) decide leaf-vs-table from the
  PTE **bits alone**. Reconciling that is a genuine seam refinement that belongs
  *with* the runtime that exercises it, not with the self-contained boot
  bring-up — so the boot tables here are hand-built block descriptors and the mm
  integration is its own slice.
- **H.7.2b — `cara_mm` runtime (allocator) + `arch_pte.h` split + FDT parse
  (DONE).** `include/cara/arch_pte.h` is now a `CARA_ARCH` dispatcher over
  `cara/arch/{riscv64,arm64}/arch_pte.h` (host + rv64 default to Sv39 with no
  flag churn; the arm64 kernel flags define `CARA_ARCH_ARM64`). `cara_fdt` +
  `cara_mm` are in the AArch64 link, and the arm64 entry parses the real QEMU
  device tree (discovered at RAM base, since the ELF `-kernel` path leaves
  `x0=0`), builds the `PhysMap`, and inits the page allocator + heap — all
  through the SAME shared code the RISC-V kernel uses (256 MiB seen, allocator +
  `Croi_Alloc` round-trip verified). The smoke asserts the "mm up" milestone.
  *Still deferred to H.7.4 (the consumer):* the generic-walk level→leaf/table
  reconciliation + `Page_Map`/`Croi_NewKernelPT` correctness + `arch_mmu_*`.
  H.7.2b uses only the allocator + physmap, which never touch the PTE encoding,
  so nothing exercises the walk yet. (`arch/arm64/arch_pte.h` documents the gap.)
- **H.7.3 — trap + syscall (DONE).** `cara/trap.h`'s `struct TrapFrame` is now
  `CARA_ARCH`-selected (AArch64 variant: x0–x30 + sp/elr/spsr/esr + a synthetic
  `kind`, 288 B). `arch/arm64/trap_entry.S` is the 16-entry `VBAR_EL1` table +
  common save/restore + `eret`; `arch/arm64/trap.c` does `ESR_EL1.EC` classify
  (`arch_trap_is_syscall` = sync vector + EC 0x15), the svc ABI (`arch_syscall_*`
  = x8 num / x0–x5 args / x0 ret), `arch_trap_init` (`VBAR_EL1`), and an
  EC-decoded `arch_trap_fatal`. The **portable** `Croi_TrapDispatch`
  (`src/croi/trap.c`) is linked unchanged and drives it. Verified by issuing an
  `svc` from EL1 and checking the round-trip. Two AArch64 specifics handled: an
  IRQ is told from a sync trap by *vector* (the `kind` tag), not a cause bit; and
  `svc` leaves `ELR` already past itself, so `arch_trap_advance` is a no-op.
  *Temporary:* `arch/arm64/trap_demo.c` stands in for `Croi_Syscall_Dispatch`
  (echo) + `Croi_Time_OnTimerTrap` (stub) until `cara_syscall`/`cara_time` are
  linked in H.7.4 — delete it then.
- **H.7.4 — per-task address spaces + context switch + FP + enter U-mode.**
  Split into sub-slices (each green):
  - **H.7.4a — per-task page tables + `arch_mmu_*` (DONE).** `arch/arm64/mmu.c`:
    `arch_mmu_activate` (swap `TTBR0_EL1` + ASID; the kernel keeps running out of
    the fixed `TTBR1`), `arch_mmu_fence`/`fence_va` (`TLBI`), `arch_mmu_boot_root`
    (TTBR1). `Croi_NewKernelPT` is arch-gated: the AArch64 per-task root is the
    TTBR0 (user/low) root only — empty, since the kernel half is the shared
    TTBR1 (no Sv39-style kernel-half clone). The generic walk (`pt.c`) runs
    **unchanged** — validated by a `Page_Map` + `arch_mmu_activate` round-trip
    (write via TTBR0, read back via TTBR0 and the TTBR1 direct map). The
    "level-aware leaf" worry from H.7.2b resolved itself: the walk only queries
    `arch_pte_is_leaf` at upper levels (block detection), where per-task entries
    are always tables, so the bits-only test is correct (note in
    `arch/arm64/arch_pte.h`). The PL011 console moved to its TTBR1 alias so it
    survives TTBR0 switches.
  - **H.7.4b — context-switch primitive (DONE).** `arch/arm64/ctx_switch.S`
    `arch_ctx_switch` saves/restores the callee-saved set (x19–x30, incl.
    x29=FP / x30=LR), sp, and tpidr_el1; `ret` lands in the incoming context's
    saved x30. `struct Task`'s saved areas (`exec/tasks.h`) are now
    `CARA_ARCH`-selected (arm64: `TASK_NSAVED=16`, `TASK_NFPSAVE=66` for NEON
    v0–v31; RISC-V 17/33 unchanged). Validated by a standalone two-context
    round-trip (switch out to a second context primed by hand, it switches back).
    FP save/restore is deferred to H.7.4c (kernel tasks are integer-only).
    *Scheduler note:* the full `cara_sched` is NOT linked yet — it pulls the
    SASOS shared heap (`shared.c`, RISC-V-only so far), `cara_log`, and
    `cara_exec_lib_image` via its user-spawn paths. So the `arch_ctx_init_kernel/
    user` + `task_trampoline` + real-scheduler integration come in a dedicated
    later slice once those subsystems are ported (the hand-prime in the demo is
    exactly what `arch_ctx_init_kernel` will do).
  - **H.7.4c — FP / NEON context (DONE).** `_start.S` enables FP/SIMD at EL1+EL0
    (`CPACR_EL1.FPEN=0b11`); `arch_ctx_switch` now round-trips the whole NEON
    file (q0–q31 + fpcr/fpsr) on every switch (the `.S` uses `.arch armv8-a` so
    the FP instructions assemble despite the kernel's `-mgeneral-regs-only`).
    Validated by the ctx demo: seed v0, switch out to a context that clobbers it,
    switch back, v0 restored ("fp: preserved ok").
  - **H.7.4d — enter-U-mode (EL0).** The EL0 `eret` entry: build a user PT
    mapping an EL0 stub + stack, `arch_mmu_activate` it, set
    SPSR_EL1=EL0t/ELR_EL1/sp_el0, `eret` to EL0; the stub `svc`s back (the
    "Lower EL AArch64" vector). Prove it standalone with the `arch_ctx_switch`
    bracket (save the kernel context, run the EL0 excursion, the exit-svc handler
    switches back) — no scheduler needed. `arch_ctx_init_user` /
    `user_task_trampoline` (which read `Sched_Current`) + deleting `trap_demo.c`
    + the real `cara_syscall`/`cara_time` wait for the scheduler integration.
- **H.7.5 — timer + IRQ + PSCI.** Generic timer (`CNTV_*`), GIC IRQ path,
  PSCI halt/off; wire `arch_timer_*` + `arch_irq_*` + the scheduler tick.
- **H.7.6 — syscall trampolines + first U-mode program.** Factor
  `CARA_SYSCALL_TRAMPOLINE` to an arch include (`svc`); run a first U-mode
  program on ARM64.
- **H.7.7+ — full portable kernel + libraries + a real app.** Enable the
  remaining shared libs + userland for AArch64, an arm64 in-kernel test runner,
  and boot-smoke parity (banner + `0 failed`). Port `croi.lds` cleanly to a
  `CARA_ARCH`-selected linker script.

## 6. Per-slice gate

Same as every epic: **RISC-V stays green** (rv64 builds, two-boot QEMU smoke
`ok`, in-kernel runner `0 failed`, host `ctest`, `format-check`), **plus** the
ARM64 progress for that slice (builds + boots as far as the slice reaches under
`qemu-system-aarch64`). Then a follow-up commit updates `docs/HANDOFF.md` + the
session memory. `docs/LVO_COVERAGE.md` regenerates only if a slice touches a
`.conf` (H.7 should not). Never commit `CLAUDE.md` / `amiga_docs/`.
