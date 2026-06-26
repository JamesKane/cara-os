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
- **H.7.2 — MMU + paging + reach `croi_entry`.** arm64 `arch_pte.h` (make the
  include `CARA_ARCH`-selected); boot stage-1 PT (TTBR0 identity-low + TTBR1
  upper-half); enable MMU; jump high; bring in `cara_mm` for AArch64 and land in
  a portable `croi_entry` skeleton (FDT parse + mm init). `arch_mmu_*`.
- **H.7.3 — trap + syscall.** `VBAR_EL1` vectors, `trap_entry.S` save/restore,
  `ESR` decode, the AArch64 `TrapFrame`; `arch_trap_*` + `arch_syscall_*`; wire
  the portable `Croi_TrapDispatch` + syscall table.
- **H.7.4 — context switch + FP + enter U-mode.** `ctx_switch.S` (callee-saved +
  sp + tpidr), NEON v0–v31 + fpcr/fpsr behind `CPACR_EL1`, `context.c`
  (`arch_ctx_init_*` + the EL0 `eret` trampoline). Bring in `cara_sched`.
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
