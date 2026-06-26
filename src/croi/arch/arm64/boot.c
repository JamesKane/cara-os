// SPDX-License-Identifier: BSD-2-Clause
//
// AArch64 upper-half boot entry (epic H.7.2b, docs/ARM64.md §5).
//
// H.7.2b reaches a portable mm bring-up on AArch64: _start.S enables stage-1
// paging and branches here (upper half). This parses the real device tree and
// initialises the physical memory manager — the page allocator + heap — through
// the SAME shared cara_fdt / cara_mm code the RISC-V kernel uses, proving the
// portable memory subsystem works on a second ISA. It is still a bring-up stub
// (it does not yet construct the libraries / scheduler / userland that the full
// croi_entry does); when those are ported it is replaced by croi_entry proper.
//
// NB the page-TABLE walk (Page_Map / Croi_NewKernelPT) is NOT used here and is
// not yet correct on AArch64 — block-vs-page is encoded by level, which the
// generic walk doesn't yet know (see cara/arch/arm64/arch_pte.h). The page
// *allocator* + physmap never touch that encoding, so this path is sound. The
// walk reconciliation + arch_mmu_* land with the scheduler (H.7.4).

#include <cara/alloc.h>
#include <cara/arch.h>
#include <cara/fdt.h>
#include <cara/mm.h>
#include <cara/trap.h> // struct TrapFrame (for the demo Croi_Syscall_Dispatch)
#include <cara/types.h>
#include <exec/tasks.h> // TASK_NSAVED / TASK_NFPSAVE (saved-area sizes)

CARA_NORETURN void arm64_kernel_main(u64 dtb_phys);

// ---- H.7.4b context-switch round-trip demo --------------------------------
// Validates the arch_ctx_switch primitive (the asm register save/restore) by
// switching this context out to a second one and back. The second context is
// primed by hand — exactly what arch_ctx_init_kernel will do once the portable
// scheduler is ported (cara_sched pulls in the SASOS shared heap + log, not yet
// on arm64), so this proves the layout + the switch in isolation.
//
// saved_regs indices match arch/arm64/ctx_switch.S: x30(LR)=11, sp=12, tpidr=13.
enum { ARM64_SR_X30 = 11, ARM64_SR_SP = 12, ARM64_SR_TPIDR = 13 };

static u64 g_ctx_main[TASK_NSAVED];
static u64 g_ctx_work[TASK_NSAVED];
static u64 g_fp_main[TASK_NFPSAVE];
static u64 g_fp_work[TASK_NFPSAVE];
static u8 g_work_stack[8192] __attribute__((aligned(16)));
static volatile int g_ctx_state;

// NEON poke/peek (arch/arm64/ctx_switch.S) — set/read v0.d[0] from integer-only
// C, to prove arch_ctx_switch round-trips the FP file (H.7.4c).
extern void arm64_fp_write_v0(u64 v);
extern u64 arm64_fp_read_v0(void);

static void ctx_demo_worker(void)
{
    g_ctx_state = 2;
    // Clobber v0 in this context; arch_ctx_switch must save it here and restore
    // the main context's value when it switches back.
    arm64_fp_write_v0(0x5A5A5A5A5A5A5A5Aull);
    arch_console_puts("arm64 boot: ctxsw: running in second context\n");
    // Switch back to the main context: arch_ctx_switch saves us into g_ctx_work
    // and reloads g_ctx_main, so main resumes right after its switch call.
    arch_ctx_switch(g_ctx_work, g_ctx_main, g_fp_work, g_fp_main);
    arch_halt(); // not reached in this one-shot demo
}

// ---- H.7.4d enter-U-mode (EL0) round-trip demo ----------------------------
// Build a user page table mapping an EL0 stub + stack, eret to EL0, and let the
// stub svc back. The "exit" syscall handler switches back to the kernel boot
// flow saved before the excursion — so this validates enter-U-mode + return
// without the scheduler. (arch_ctx_init_user / user_task_trampoline read
// Sched_Current and arrive with the scheduler integration.)
#define DEMO_SYS_EXIT 0xE0ull
#define DEMO_USER_EXIT_CODE 0x55ull
#define DEMO_USER_CODE_VA 0x0000000000400000ull
#define DEMO_USER_STACK_VA 0x0000000000500000ull

extern const u8 arm64_el0_stub[];     // EL0 stub bytes (arch/arm64/ctx_switch.S)
extern const u8 arm64_el0_stub_end[]; // end marker

static u64 g_kernel_resume[TASK_NSAVED]; // boot flow to resume after EL0
static u64 g_kernel_resume_fp[TASK_NFPSAVE];
static u64 g_enter_ctx[TASK_NSAVED]; // runs enter_el0_fn (does the eret)
static u64 g_enter_fp[TASK_NFPSAVE];
static u64 g_exit_throwaway[TASK_NSAVED]; // save area for the exit switch (unused)
static u64 g_exit_throwaway_fp[TASK_NFPSAVE];
static u8 g_enter_stack[8192] __attribute__((aligned(16)));
static struct PageTable *g_user_pt;
static volatile u64 g_user_exit_code;
static volatile bool g_user_exited;

// Demo syscall dispatcher. The portable Croi_TrapDispatch (src/croi/trap.c)
// calls this on every svc. (Stands in for cara_syscall's real table-driven
// Croi_Syscall_Dispatch until the scheduler integration.)
i64 Croi_Syscall_Dispatch(struct TrapFrame *frame);
i64 Croi_Syscall_Dispatch(struct TrapFrame *frame)
{
    u64 num = arch_syscall_num(frame);
    u64 a0 = arch_syscall_arg(frame, 0);
    if (num == DEMO_SYS_EXIT) {
        g_user_exit_code = a0;
        g_user_exited = true;
        // Resume the kernel boot flow saved before entering EL0; this abandons
        // the trap frame on the EL1 stack (fine for the one-shot demo) and
        // never returns to the trap-return path.
        arch_ctx_switch(g_exit_throwaway, g_kernel_resume, g_exit_throwaway_fp, g_kernel_resume_fp);
    }
    return (i64)(num + a0); // H.7.3 EL1 svc echo (num + arg0)
}

// Runs in g_enter_ctx: switch to the user address space and eret to EL0.
CARA_NORETURN static void enter_el0_fn(void)
{
    arch_mmu_activate(g_user_pt);
    u64 ustack_top = DEMO_USER_STACK_VA + CARA_PAGE_SIZE;
    // SPSR_EL1: M[4:0]=0 (EL0t), DAIF all masked (no IRQ source until H.7.5).
    __asm__ volatile("msr sp_el0, %[usp]\n\t"
                     "msr elr_el1, %[entry]\n\t"
                     "msr spsr_el1, %[spsr]\n\t"
                     "isb\n\t"
                     "eret\n"
                     :
                     : [usp] "r"(ustack_top), [entry] "r"(DEMO_USER_CODE_VA), [spsr] "r"(0x3c0ull)
                     : "memory");
    __builtin_unreachable();
}

// QEMU `-M virt` (aarch64) RAM base + kernel load address (see kernel.lds).
// These match what _start.S already assumes (the boot block descriptors map
// PA 0x40000000); the FDT we parse below is then authoritative for the memory
// map. QEMU drops the generated DTB at the base of RAM and does NOT pass it in
// x0 for an ELF `-kernel`, so we discover it there when the boot register is 0.
#define ARM64_RAM_BASE 0x40000000ull
#define ARM64_KERNEL_PHYS_BASE 0x40200000ull

// End-of-image upper-half VA from kernel.lds; phys = VA - KERNEL_VA_OFFSET.
extern char __kernel_end_virt[];

// pt.c references these by `extern` (Page_Alloc/Free over the global allocator);
// the heap is the active allocator backing Croi_Alloc. Zeroed BSS at boot.
struct PageAllocator g_page_alloc;
struct Heap g_heap;

// Minimal "0x"-prefixed 64-bit hex (the early console only does strings and no
// printf backend exists yet — mirrors leaning on arch_console_*).
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

static void put_line(const char *label, u64 v)
{
    arch_console_puts(label);
    put_hex64(v);
    arch_console_puts("\n");
}

// Scan low RAM below the kernel image for the FDT magic (devicetree spec: the
// header's first big-endian u32 is 0xd00dfeed, i.e. 0xedfe0dd0 read little-
// endian). QEMU places the DTB at the base of RAM. Returns the phys address, or
// 0 if not found. Bounded to [RAM_BASE, kernel) so it can't hit the kernel image.
static u64 arm64_find_dtb(void)
{
    for (u64 p = ARM64_RAM_BASE; p < ARM64_KERNEL_PHYS_BASE; p += 8) {
        const volatile u32 *w = (const volatile u32 *)Mm_PhysToVirt(p);
        if (*w == 0xedfe0dd0u) {
            return p;
        }
    }
    return 0;
}

CARA_NORETURN void arm64_kernel_main(u64 dtb_phys)
{
    arch_console_puts("\nCaraOS croi \xe2\x80\x94 arch=arm64 (H.7.2b mm bring-up)\n");
    put_line("arm64 boot: arm64_kernel_main @ ", (u64)(uptr)&arm64_kernel_main);

    // ---- Acquire the DTB. Prefer the boot register; fall back to a scan. ----
    struct Fdt fdt;
    u64 dtb = dtb_phys;
    if (dtb == 0 || Fdt_Open(&fdt, Mm_PhysToVirt(dtb)) != CARA_EOK) {
        dtb = arm64_find_dtb();
        if (dtb == 0 || Fdt_Open(&fdt, Mm_PhysToVirt(dtb)) != CARA_EOK) {
            arch_console_puts("arm64 boot: FATAL: no usable device tree found\n");
            arch_halt();
        }
    }
    put_line("arm64 boot: DTB @ ", dtb);
    put_line("arm64 boot: DTB totalsize = ", fdt.totalsize);

    // ---- Physical memory map (carve out kernel image + DTB). ----
    u64 kphys_start = ARM64_KERNEL_PHYS_BASE;
    u64 kphys_end = Mm_VirtToPhys(__kernel_end_virt);
    u64 dtb_start = dtb;
    u64 dtb_end = dtb + fdt.totalsize;

    put_line("arm64 boot: kernel phys end = ", kphys_end);

    struct PhysMap pm;
    int rc = Mm_PhysMapFromFdt(&pm, &fdt, kphys_start, kphys_end, dtb_start, dtb_end);
    if (rc != CARA_EOK) {
        put_line("arm64 boot: FATAL: Mm_PhysMapFromFdt rc = ", (u64)(i64)rc);
        arch_halt();
    }
    put_line("arm64 boot: RAM total bytes  = ", pm.total_bytes);
    put_line("arm64 boot: usable bytes     = ", pm.usable_bytes);

    // ---- Page allocator. ----
    rc = Page_Init(&g_page_alloc, &pm);
    if (rc != CARA_EOK) {
        put_line("arm64 boot: FATAL: Page_Init rc = ", (u64)(i64)rc);
        arch_halt();
    }
    put_line("arm64 boot: free pages       = ", g_page_alloc.free_pages);

    // Round-trip a single page through the allocator to prove it works.
    u64 pg = Page_Alloc(&g_page_alloc, 1);
    if (pg == 0) {
        arch_console_puts("arm64 boot: FATAL: Page_Alloc returned 0\n");
        arch_halt();
    }
    put_line("arm64 boot: Page_Alloc(1)    = ", pg);
    Page_Free(&g_page_alloc, pg, 1);

    // ---- Kernel heap on top of the allocator. ----
    rc = Heap_Init(&g_heap, &g_page_alloc);
    if (rc != CARA_EOK) {
        put_line("arm64 boot: FATAL: Heap_Init rc = ", (u64)(i64)rc);
        arch_halt();
    }
    Heap_SetActive(&g_heap);
    void *blk = Croi_Alloc(128);
    if (!blk) {
        arch_console_puts("arm64 boot: FATAL: Croi_Alloc(128) failed\n");
        arch_halt();
    }
    put_line("arm64 boot: Croi_Alloc(128)  = ", (u64)(uptr)blk);

    // ---- Trap + syscall path (H.7.3). Install VBAR_EL1, then issue an svc
    //      from EL1 and check the round-trip: the vector saves a TrapFrame, the
    //      portable Croi_TrapDispatch classifies it (ESR.EC = SVC) and routes
    //      to the (temporary) demo dispatch, which returns x8 + x0. We pass
    //      x8 = 0x42, x0 = 0x100 and expect x0 = 0x142 on return.
    arch_trap_init();
    {
        register u64 r8 __asm__("x8") = 0x42ull;
        register u64 r0 __asm__("x0") = 0x100ull;
        __asm__ volatile("svc #0" : "+r"(r0) : "r"(r8) : "memory");
        put_line("arm64 boot: svc returned    = ", r0);
        if (r0 == 0x142ull) {
            arch_console_puts("arm64 boot: trap: svc ok\n");
        } else {
            arch_console_puts("arm64 boot: FATAL: svc round-trip mismatch\n");
            arch_halt();
        }
    }

    // ---- Per-task page tables (H.7.4a). Build a fresh page table, map a page
    //      into its TTBR0 (user/low) half via the generic walk, activate it,
    //      and round-trip a value: write through the user VA (resolved by the
    //      new TTBR0) and read it back through both the user VA and the kernel
    //      direct map (TTBR1) — proving the walk + activate land on the same
    //      physical page, and that switching TTBR0 leaves the kernel (TTBR1)
    //      running (we keep printing afterwards via the console's TTBR1 alias).
    {
        struct PageTable *pt = Croi_NewKernelPT();
        if (!pt) {
            arch_console_puts("arm64 boot: FATAL: Croi_NewKernelPT failed\n");
            arch_halt();
        }
        u64 test_va = 0x10000000ull; // user/low half (TTBR0)
        u64 test_pa = Page_Alloc(&g_page_alloc, 1);
        if (test_pa == 0) {
            arch_console_puts("arm64 boot: FATAL: Page_Alloc (PT test) failed\n");
            arch_halt();
        }
        rc = Page_Map(pt, test_va, test_pa, PTE_KERNEL_RW);
        if (rc != CARA_EOK) {
            put_line("arm64 boot: FATAL: Page_Map rc = ", (u64)(i64)rc);
            arch_halt();
        }
        arch_mmu_activate(pt);

        const u64 pattern = 0xCAFEBABEDEADBEEFull;
        *(volatile u64 *)(uptr)test_va = pattern;
        u64 via_ttbr0 = *(volatile u64 *)(uptr)test_va;
        u64 via_ttbr1 = *(volatile u64 *)Mm_PhysToVirt(test_pa);
        put_line("arm64 boot: PT va             = ", test_va);
        put_line("arm64 boot: PT read (TTBR0)   = ", via_ttbr0);
        put_line("arm64 boot: PT read (TTBR1)   = ", via_ttbr1);
        if (via_ttbr0 == pattern && via_ttbr1 == pattern) {
            arch_console_puts("arm64 boot: pagetable: map ok\n");
        } else {
            arch_console_puts("arm64 boot: FATAL: page-table round-trip mismatch\n");
            arch_halt();
        }
    }

    // ---- Context switch (H.7.4b). Prime a second context to start at
    //      ctx_demo_worker on its own stack, switch to it, and let it switch
    //      back — proving arch_ctx_switch round-trips the callee-saved set,
    //      sp, and tpidr correctly.
    {
        u64 wtop = ((u64)(uptr)g_work_stack + sizeof g_work_stack) & ~15ull;
        g_ctx_work[ARM64_SR_X30] = (u64)(uptr)&ctx_demo_worker;
        g_ctx_work[ARM64_SR_SP] = wtop;
        g_ctx_work[ARM64_SR_TPIDR] = 0;
        g_ctx_state = 1;
        // Seed v0 so the round-trip can prove the NEON file is preserved across
        // the switch (the worker clobbers v0 with a different value).
        arm64_fp_write_v0(0xA5A5A5A5A5A5A5A5ull);
        arch_console_puts("arm64 boot: ctxsw: switching to second context\n");
        arch_ctx_switch(g_ctx_main, g_ctx_work, g_fp_main, g_fp_work);
        // Resumes here when the worker switches back.
        u64 v0 = arm64_fp_read_v0();
        if (g_ctx_state == 2) {
            arch_console_puts("arm64 boot: ctxsw: round-trip ok\n");
        } else {
            arch_console_puts("arm64 boot: FATAL: ctx round-trip mismatch\n");
            arch_halt();
        }
        put_line("arm64 boot: fp v0 after switch = ", v0);
        if (v0 == 0xA5A5A5A5A5A5A5A5ull) {
            arch_console_puts("arm64 boot: fp: preserved ok\n");
        } else {
            arch_console_puts("arm64 boot: FATAL: fp not preserved across switch\n");
            arch_halt();
        }
    }

    // ---- Enter U-mode (H.7.4d). Build a user PT mapping an EL0 stub + stack,
    //      eret to EL0, and let the stub svc back. The exit-svc handler
    //      (Croi_Syscall_Dispatch) switches back to the boot flow saved here.
    {
        g_user_pt = Croi_NewKernelPT();
        u64 code_pa = Page_Alloc(&g_page_alloc, 1);
        u64 stack_pa = Page_Alloc(&g_page_alloc, 1);
        if (!g_user_pt || code_pa == 0 || stack_pa == 0) {
            arch_console_puts("arm64 boot: FATAL: U-mode PT/page alloc failed\n");
            arch_halt();
        }
        // Copy the EL0 stub into the code page (via the kernel direct map), then
        // make it visible to instruction fetch (clean D, invalidate I).
        u8 *code = (u8 *)Mm_PhysToVirt(code_pa);
        usize stub_len = (usize)(arm64_el0_stub_end - arm64_el0_stub);
        for (usize i = 0; i < stub_len; i++) {
            code[i] = arm64_el0_stub[i];
        }
        __asm__ volatile("dc cvau, %0\n\tdsb ish\n\tic iallu\n\tdsb ish\n\tisb"
                         :
                         : "r"(code)
                         : "memory");
        if (Page_Map(g_user_pt, DEMO_USER_CODE_VA, code_pa, PTE_USER_RX) != CARA_EOK ||
            Page_Map(g_user_pt, DEMO_USER_STACK_VA, stack_pa, PTE_USER_RW) != CARA_EOK) {
            arch_console_puts("arm64 boot: FATAL: U-mode Page_Map failed\n");
            arch_halt();
        }

        u64 etop = ((u64)(uptr)g_enter_stack + sizeof g_enter_stack) & ~15ull;
        g_enter_ctx[ARM64_SR_X30] = (u64)(uptr)&enter_el0_fn;
        g_enter_ctx[ARM64_SR_SP] = etop;
        g_enter_ctx[ARM64_SR_TPIDR] = 0;
        arch_console_puts("arm64 boot: entering EL0 user mode...\n");
        arch_ctx_switch(g_kernel_resume, g_enter_ctx, g_kernel_resume_fp, g_enter_fp);
        // Resumes here after the EL0 stub's exit-svc switches back.
        put_line("arm64 boot: user exit code   = ", g_user_exit_code);
        if (g_user_exited && g_user_exit_code == DEMO_USER_EXIT_CODE) {
            arch_console_puts("arm64 boot: enter-U-mode: ok\n");
        } else {
            arch_console_puts("arm64 boot: FATAL: U-mode round-trip mismatch\n");
            arch_halt();
        }
    }

    arch_console_puts("CaraOS arm64 boot: ok (paged + mm + traps + pt + ctx + fp + EL0)\n");
    arch_halt();
}
