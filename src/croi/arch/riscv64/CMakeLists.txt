# SPDX-License-Identifier: BSD-2-Clause
#
# RISC-V (Sv39, S-mode) HAL backend — the arch/<arch>/ unit selected by
# CARA_ARCH (docs/ARCH_HAL.md, epic H). Contributes its sources directly to
# the croi executable (defined in the parent) rather than a static library, so
# the boot entry (_start) and the trap/ctx asm land in croi without
# archive-ordering surprises. A second ISA (arm64, H.7) ships a sibling
# arch/arm64/ with the same arch_* surface (cara/arch.h, cara/arch_pte.h).
target_sources(croi PRIVATE
    _start.S       # boot: hart setup, boot page table, jump to croi_entry
    trap_entry.S   # trap save/restore → Croi_TrapDispatch
    ctx_switch.S   # arch_ctx_switch (integer + FP file)
    cpu.c          # arch_halt / arch_idle / arch_irq_enable/disable
    timer.c        # arch_timer_ticks / arm / disarm
    firmware.c     # arch_console_putc/puts (SBI)
    mmu.c          # arch_mmu_activate / fence / fence_va / boot_root
    context.c      # user_task_trampoline + arch_ctx_init_kernel/user
    trap.c         # scause classify + syscall ABI + arch_trap_init/fatal
)
