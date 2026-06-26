// SPDX-License-Identifier: BSD-2-Clause
//
// AArch64 boot stub (epic H.7.1, docs/ARM64.md §5).
//
// THIS IS A BRING-UP STUB. H.7.1 proves the arm64 toolchain, linker script,
// QEMU `-M virt` handoff, and the PL011 early console end to end: _start.S
// brings the CPU to EL1 with the MMU off and tail-calls here, which writes a
// banner and halts. It does NOT yet hand off to the portable croi_entry — that
// needs stage-1 paging + the upper-half jump (H.7.2). When paging lands,
// arm64_boot_main is replaced by the MMU bring-up that ends in croi_entry,
// exactly as arch/riscv64/_start.S's _high_entry calls croi_entry today.

#include <cara/arch.h>
#include <cara/types.h>

CARA_NORETURN void arm64_boot_main(u64 dtb_phys);

CARA_NORETURN void arm64_boot_main(u64 dtb_phys)
{
    (void)dtb_phys; // consumed once the FDT parser runs (H.7.2)

    arch_console_puts("\nCaraOS croi \xe2\x80\x94 arch=arm64 (H.7 bring-up)\n");
    arch_console_puts("arm64 boot: EL1 reached, MMU off, PL011 console up\n");
    arch_console_puts("CaraOS arm64 boot: ok\n");

    arch_halt();
}
