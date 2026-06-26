# SPDX-License-Identifier: BSD-2-Clause
#
# ARM64 (AArch64, EL1) HAL backend — the arch/<arch>/ unit selected by
# CARA_ARCH=arm64 (docs/ARM64.md, epic H.7). Like arch/riscv64, it contributes
# its sources directly to the croi executable (defined in the parent) rather
# than a static library, so the boot entry (_start) lands in croi without
# archive-ordering surprises.
#
# H.7.2 brings up stage-1 paging: _start builds the boot tables, enables the
# MMU, and branches to the upper-half _high_entry, which calls the C entry
# (running at SASOS upper-half VAs). The trap / ctx / timer / cara_mm-runtime
# seams arrive in later H.7.x slices.
target_sources(croi PRIVATE
    _start.S       # boot: EL1 bring-up, stage-1 boot PT, MMU on, jump upper half
    firmware.c     # arch_console_putc/puts (PL011 early console)
    cpu.c          # arch_halt / arch_idle / arch_irq_enable/disable
    boot.c         # arm64_kernel_main: FDT + mm bring-up + svc demo (stub)
    trap_entry.S   # VBAR_EL1 vectors + save/restore → Croi_TrapDispatch (H.7.3)
    trap.c         # ESR classify + syscall ABI + arch_trap_init/fatal (H.7.3)
    trap_demo.c    # TEMPORARY demo dispatch deps — removed in H.7.4
)
