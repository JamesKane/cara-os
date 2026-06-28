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
#include <cara/log.h> // structured logging (H.7.7b)
#include <cara/mm.h>
#include <cara/sched.h>  // cooperative scheduler (H.7.7d)
#include <cara/shared.h> // SASOS shared heap (H.7.7c)
#include <cara/sysno.h>  // SYS_LOG_WRITE / SYS_EXIT (H.7.7e)
#include <cara/time.h>   // portable Croi_Time layer (H.7.7a)
#include <cara/trap.h>   // struct TrapFrame (for the demo Croi_Syscall_Dispatch)
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

// Demo syscall trampoline built by the factored CARA_SYSCALL_TRAMPOLINE macro
// (arch/arm64/trampoline_demo.S, H.7.6) — sets x8 and svc's.
extern u64 Cara_DemoEchoTrampoline(u64 arg);

// Timer + GIC + PSCI (arch/arm64/timer.c, psci.c, H.7.5).
extern void arm64_gic_init(void);
extern u64 arm64_timer_freq(void);
extern void arm64_psci_init(bool use_hvc);
CARA_NORETURN extern void arm64_psci_system_off(void);

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

// ---- H.7.7d real-scheduler kernel-task demo -------------------------------
static volatile int g_sched_demo_done;

static void sched_demo_worker(void *arg)
{
    arch_console_puts((u64)(uptr)arg == 1 ? "arm64 boot: sched: worker A running\n"
                                          : "arm64 boot: sched: worker B running\n");
    g_sched_demo_done++;
    // Return → task_trampoline → Sched_Trampoline → Croi_TaskExit.
}

// ---- U-mode (EL0) demo program + syscall dispatch (H.7.7e) -----------------
// A real U-mode task, run by the scheduler (Croi_SpawnUserTask → the EL0
// user_task_trampoline), executes the arm64_el0_stub program at EL0: it issues
// SYS_LOG_WRITE then SYS_EXIT. The H.7.3 echo (num 0x42, from the EL1 svc
// trampoline test) is still handled here. This is a stand-in for cara_syscall's
// real table-driven Croi_Syscall_Dispatch, which arrives with the libraries
// (H.7.7f+) — linking that today would pull in the whole library surface.
#define DEMO_SYS_ECHO 0x42ull
#define DEMO_USER_EXIT_CODE 0x77ull

extern const u8 arm64_el0_stub[];     // EL0 program bytes (arch/arm64/ctx_switch.S)
extern const u8 arm64_el0_stub_end[]; // end marker

static volatile u64 g_user_exit_code;
static volatile bool g_user_exited;

// Minimal syscall dispatcher. The portable Croi_TrapDispatch (src/croi/trap.c)
// calls this on every svc.
i64 Croi_Syscall_Dispatch(struct TrapFrame *frame);
i64 Croi_Syscall_Dispatch(struct TrapFrame *frame)
{
    u64 num = arch_syscall_num(frame);
    switch (num) {
    case SYS_LOG_WRITE: {
        // (x0 = user VA of bytes, x1 = length). The faulting task's TTBR0 is
        // active, so the kernel reads the user buffer directly (PAN=0).
        const char *p = (const char *)(uptr)arch_syscall_arg(frame, 0);
        u64 len = arch_syscall_arg(frame, 1);
        for (u64 i = 0; i < len; i++) {
            if (p[i] == '\n') {
                arch_console_putc('\r');
            }
            arch_console_putc(p[i]);
        }
        return (i64)len;
    }
    case SYS_EXIT:
        g_user_exit_code = arch_syscall_arg(frame, 0);
        g_user_exited = true;
        Croi_TaskExit(); // [[noreturn]] — schedules the next task; never returns
        return 0;        // unreachable
    case DEMO_SYS_ECHO:
        return (i64)(num + arch_syscall_arg(frame, 0)); // H.7.3 EL1 svc echo
    default:
        return -1;
    }
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

// LogSink that writes formatted records to the PL011 console (H.7.7b) — the
// AArch64 counterpart of Log_Sink_NS16550_Emit (which drives the RISC-V UART).
static void arm64_log_console_emit(const struct LogRecord *r, void *ctx)
{
    (void)ctx;
    char buf[CARA_LOG_RECORD_BYTES + 64];
    usize n = Log_FormatHuman(buf, sizeof buf, r, /*ansi=*/false);
    for (usize i = 0; i < n; i++) {
        if (buf[i] == '\n') {
            arch_console_putc('\r');
        }
        arch_console_putc(buf[i]);
    }
}

static struct LogSink g_console_sink; // persists for the kernel's lifetime

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

    // PSCI conduit from the FDT /psci "method" ("hvc" → HVC, else SMC).
    {
        u32 psci_node;
        bool use_hvc = true;
        if (Fdt_ResolvePath(&fdt, "/psci", &psci_node) == CARA_EOK) {
            const char *method = Fdt_PropStr(&fdt, psci_node, "method");
            if (method) {
                use_hvc = (method[0] == 'h');
            }
        }
        arm64_psci_init(use_hvc);
    }

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

    // ---- Structured logging (H.7.7b). Log_Init allocates the ring from the
    //      heap (now active); register a PL011 console sink so LOG_* records
    //      reach the console, then prove a record flows through.
    if (Log_Init() != CARA_EOK) {
        arch_console_puts("arm64 boot: FATAL: Log_Init failed\n");
        arch_halt();
    }
    g_console_sink = (struct LogSink){ .emit = arm64_log_console_emit,
                                       .ctx = nullptr,
                                       .ansi_capable = false,
                                       .min_level = (u8)LOG_LV_TRACE };
    if (Log_RegisterSink(&g_console_sink) != CARA_EOK) {
        arch_console_puts("arm64 boot: FATAL: Log_RegisterSink failed\n");
        arch_halt();
    }
    LOG_INFO("boot", "arm64 logging up");

    // ---- Trap + syscall path (H.7.3). Install VBAR_EL1, then issue an svc
    //      from EL1 and check the round-trip: the vector saves a TrapFrame, the
    //      portable Croi_TrapDispatch classifies it (ESR.EC = SVC) and routes
    //      to the (temporary) demo dispatch, which returns x8 + x0. We pass
    //      x8 = 0x42, x0 = 0x100 and expect x0 = 0x142 on return.
    arch_trap_init();
    {
        // Issue the demo syscall through the factored CARA_SYSCALL_TRAMPOLINE
        // (arch/arm64/trampoline_demo.S): it sets x8 = 0x42 and svc's; the demo
        // dispatcher echoes num + arg0, so 0x100 → 0x142.
        u64 r = Cara_DemoEchoTrampoline(0x100ull);
        put_line("arm64 boot: svc returned    = ", r);
        if (r == 0x142ull) {
            arch_console_puts("arm64 boot: trap: svc ok (via trampoline)\n");
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

    // ---- SASOS shared heap (H.7.7c). Init the shared arena, then allocate from
    //      it and round-trip a value through the returned low/TTBR0 VA — the
    //      RW+U window both the kernel (here) and U-mode dereference. On arm64
    //      Croi_Shared_Init activates a kernel TTBR0 carrying the shared subtree.
    {
        if (Croi_Shared_Init(&g_page_alloc) != CARA_EOK) {
            arch_console_puts("arm64 boot: FATAL: Croi_Shared_Init failed\n");
            arch_halt();
        }
        void *s = Croi_AllocShared(256);
        if (!s) {
            arch_console_puts("arm64 boot: FATAL: Croi_AllocShared failed\n");
            arch_halt();
        }
        const u64 pattern = 0xCAFED00DFEEDFACEull;
        *(volatile u64 *)s = pattern;
        u64 rb = *(volatile u64 *)s;
        put_line("arm64 boot: shared alloc @    = ", (u64)(uptr)s);
        if (rb == pattern && (u64)(uptr)s >= CARA_SHARED_VA_BASE) {
            arch_console_puts("arm64 boot: shared heap: ok\n");
        } else {
            arch_console_puts("arm64 boot: FATAL: shared heap round-trip mismatch\n");
            arch_halt();
        }
    }

    // ---- Scheduler (H.7.7d). Init the real cooperative scheduler, spawn two
    //      kernel tasks, lower kmain below them, and yield until both have run +
    //      exited — exercising Sched_Init / Croi_SpawnKernelTask / the real
    //      arch_ctx_switch path (task_trampoline → Sched_Trampoline → entry_fn →
    //      Croi_TaskExit) on AArch64.
    {
        Sched_Init();
        g_sched_demo_done = 0;
        (void)Croi_SpawnKernelTask("wrk-a", 10, sched_demo_worker, (void *)(uptr)1);
        (void)Croi_SpawnKernelTask("wrk-b", 10, sched_demo_worker, (void *)(uptr)2);
        Croi_TaskSetSelfPriority(0); // below the workers so Yield switches to them
        while (g_sched_demo_done < 2) {
            Croi_Yield();
        }
        arch_console_puts("arm64 boot: sched: kernel tasks ok\n");
    }

    // ---- First real U-mode program (H.7.7e). Spawn a U-mode task whose text is
    //      the EL0 program (arm64_el0_stub), run by the real scheduler:
    //      user_task_trampoline `eret`s to EL0, the program SYS_LOG_WRITEs a line
    //      and SYS_EXITs, and the syscall dispatcher ends it via Croi_TaskExit —
    //      back to kmain. Needs the scheduler (above) + the shared heap (the
    //      Process lives there) to be up.
    {
        g_user_exited = false;
        usize stub_len = (usize)(arm64_el0_stub_end - arm64_el0_stub);
        // Croi_SpawnUserTask maps the page(s) containing the text at
        // CARA_USER_TEXT_BASE, so the entry is offset by the program's position
        // within its (kernel-image) page.
        u64 entry = CARA_USER_TEXT_BASE + ((u64)(uptr)arm64_el0_stub & (CARA_PAGE_SIZE - 1));
        struct Task *u = Croi_SpawnUserTask("u-demo", 10, arm64_el0_stub, stub_len, entry);
        if (!u) {
            arch_console_puts("arm64 boot: FATAL: Croi_SpawnUserTask failed\n");
            arch_halt();
        }
        while (!g_user_exited) {
            Croi_Yield();
        }
        put_line("arm64 boot: user exit code    = ", g_user_exit_code);
        if (g_user_exit_code == DEMO_USER_EXIT_CODE) {
            arch_console_puts("arm64 boot: user task: ok\n");
        } else {
            arch_console_puts("arm64 boot: FATAL: user task exit mismatch\n");
            arch_halt();
        }
    }

    // ---- Timer + IRQ via the portable Croi_Time layer (H.7.5 / H.7.7a). Bring
    //      up the GIC, then drive the portable one-shot deadline API: each
    //      SetDeadline arms the virtual timer, the IRQ (arm64_irq_dispatch →
    //      Croi_Time_OnTimerTrap) flags it, and DeadlineFired clears it.
    {
        arm64_gic_init();
        u64 freq = arm64_timer_freq();
        put_line("arm64 boot: timer freq (Hz)   = ", freq);
        Croi_Time_Init(freq); // disarm + arch_irq_enable
        u64 t0 = Croi_Time_Now();
        for (int i = 0; i < 5; i++) {
            Croi_Time_SetDeadline(Croi_Time_Now() + 10000000ull); // +10 ms
            while (!Croi_Time_DeadlineFired()) {
                __asm__ volatile("wfi");
            }
        }
        u64 t1 = Croi_Time_Now();
        arch_irq_disable();
        put_line("arm64 boot: uptime ns (start) = ", t0);
        put_line("arm64 boot: uptime ns (end)   = ", t1);
        if (t1 > t0) {
            arch_console_puts("arm64 boot: timer: deadlines ok\n");
        } else {
            arch_console_puts("arm64 boot: FATAL: time did not advance\n");
            arch_halt();
        }
    }

    arch_console_puts("CaraOS arm64 boot: ok (paged + mm + traps + pt + ctx + fp + EL0 + irq)\n");

    // Clean power-off via PSCI (QEMU exits) — the AArch64 counterpart of SBI
    // SYSTEM_RESET. Falls back to a wfi park inside if the firmware ignores it.
    arm64_psci_system_off();
}
