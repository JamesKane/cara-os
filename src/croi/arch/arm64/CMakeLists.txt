# SPDX-License-Identifier: BSD-2-Clause
#
# ARM64 (AArch64, EL1) HAL backend — the arch/<arch>/ unit selected by
# CARA_ARCH=arm64 (docs/ARM64.md, epic H.7). Like arch/riscv64, it contributes
# its sources directly to the croi executable (defined in the parent) rather
# than a static library, so the boot entry (_start) lands in croi without
# archive-ordering surprises.
#
# H.7.1 is boot-to-print only: _start brings the CPU to a known EL1 state and
# calls a boot stub that writes a banner to the PL011 console, then halts. The
# trap / ctx / mmu / timer seams arrive in later H.7.x slices.
target_sources(croi PRIVATE
    _start.S       # boot: EL1 bring-up, stack, BSS zero, call arm64_boot_main
    firmware.c     # arch_console_putc/puts (PL011 early console)
    cpu.c          # arch_halt / arch_idle / arch_irq_enable/disable
    boot.c         # arm64_boot_main: H.7.1 banner + halt (bring-up stub)
)
