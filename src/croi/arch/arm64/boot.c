// SPDX-License-Identifier: BSD-2-Clause
//
// AArch64 upper-half boot entry (epic H.7.2, docs/ARM64.md §5).
//
// STILL A BRING-UP STUB. H.7.2 brings up stage-1 paging: _start.S builds the
// boot tables, enables the MMU, and branches to _high_entry (upper half), which
// sets the stack, zeroes BSS, and calls here. So this runs at SASOS upper-half
// VAs (KERNEL_VA_OFFSET + phys) under translation — proven by printing its own
// code address, which must carry the 0xFFFFFFC0_ upper-half prefix.
//
// It does NOT yet hand off to the portable croi_entry: that needs the cara_mm
// runtime (page allocator + the generic walk), which in turn needs the
// arch_pte.h split reconciled for AArch64 (block-vs-page is encoded by level,
// unlike Sv39's bits-only leaf test) — that is the next slice. When it lands,
// arm64_kernel_main is replaced by FDT parse + mm init + the rest of
// croi_entry, exactly as arch/riscv64 reaches croi_entry today.

#include <cara/arch.h>
#include <cara/types.h>

CARA_NORETURN void arm64_kernel_main(u64 dtb_phys);

// Minimal "0x"-prefixed 64-bit hex, since the early console only does strings
// and no printf backend exists this early (mirrors leaning on arch_console_*).
static void put_hex64(u64 v)
{
    static const char digits[] = "0123456789abcdef";
    char buf[2 + 16 + 1];
    buf[0] = '0';
    buf[1] = 'x';
    for (int i = 0; i < 16; i++) {
        buf[2 + i] = digits[(v >> ((15 - i) * 4)) & 0xf];
    }
    buf[18] = '\0';
    arch_console_puts(buf);
}

CARA_NORETURN void arm64_kernel_main(u64 dtb_phys)
{
    arch_console_puts("\nCaraOS croi \xe2\x80\x94 arch=arm64 (H.7.2 paging)\n");
    arch_console_puts("arm64 boot: MMU on, executing in the SASOS upper half\n");

    arch_console_puts("arm64 boot: arm64_kernel_main @ ");
    put_hex64((u64)(uptr)&arm64_kernel_main);
    arch_console_puts("\n");

    arch_console_puts("arm64 boot: DTB phys @ ");
    put_hex64(dtb_phys);
    arch_console_puts("\n");

    arch_console_puts("CaraOS arm64 boot: ok (paged)\n");

    arch_halt();
}
