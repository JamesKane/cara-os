// SPDX-License-Identifier: BSD-2-Clause
//
// Cooperative kernel scheduler. A single priority-sorted run queue;
// Sched_Current points at the running task; Croi_Yield is the only
// switch trigger. Single hart for now (SMP arrives in Epic H).

#include <cara/alloc.h>
#include <cara/kobj.h>
#include <cara/list.h>
#include <cara/loader.h>
#include <cara/log.h>
#include <cara/sched.h>
#include <cara/types.h>

extern void croi_ctx_switch(u64 *from, u64 *to);
extern void task_trampoline(void);
extern void user_task_trampoline(void);
void Sched_Trampoline(void);

static struct Task    *g_current = nullptr;
static struct MinList  g_runq;
static struct MinList  g_sleepers;          // tasks blocked in Croi_Wait
static struct MinList  g_dead;
static bool            g_inited = false;

static struct Task *node_to_task(struct MinNode *n)
{
    return MinList_NodeOf(n, struct Task, sched_node);
}

// Copy a name into a Task's private buffer and point ln_Name at it.
// Bounded by CARA_TASK_NAME_LEN; truncated if longer.
static void task_set_name(struct Task *t, const char *src)
{
    usize i = 0;
    while (src && i + 1 < CARA_TASK_NAME_LEN && src[i]) {
        t->_ln_name_buf[i] = src[i];
        i++;
    }
    t->_ln_name_buf[i] = 0;
    t->tc_Node.ln_Name = t->_ln_name_buf;
}

// Initialize the V36+ tc_Node fields common to every spawned Task.
static void task_init_node(struct Task *t, const char *name, BYTE pri)
{
    t->tc_Node.ln_Type = NT_TASK;
    t->tc_Node.ln_Pri  = pri;
    task_set_name(t, name);
    t->tc_IDNestCnt = -1;
    t->tc_TDNestCnt = -1;
    // V36+ NewList semantics for the per-task tracked-allocations list.
    t->tc_MemEntry.lh_Head     = (struct Node *)&t->tc_MemEntry.lh_Tail;
    t->tc_MemEntry.lh_Tail     = nullptr;
    t->tc_MemEntry.lh_TailPred = (struct Node *)&t->tc_MemEntry;
    t->tc_MemEntry.lh_Type     = NT_MEMORY;
}

// Clamp an i32 priority caller-spec to BYTE (V36+ ln_Pri width).
static BYTE clamp_pri(i32 pri)
{
    if (pri < -128) return -128;
    if (pri >  127) return  127;
    return (BYTE)pri;
}

// Insert t into runq sorted by priority descending. Equal-pri tasks
// land at the tail of their priority chain → round-robin within a
// priority level.
static void runq_add(struct Task *t)
{
    for (struct MinNode *p = g_runq.mlh_Head; p->mln_Succ; p = p->mln_Succ) {
        struct Task *q = node_to_task(p);
        if (q->tc_Node.ln_Pri < t->tc_Node.ln_Pri) {
            t->sched_node.mln_Pred = p->mln_Pred;
            t->sched_node.mln_Succ = p;
            p->mln_Pred->mln_Succ = &t->sched_node;
            p->mln_Pred = &t->sched_node;
            return;
        }
    }
    MinList_AddTail(&g_runq, &t->sched_node);
}

void Sched_Init(void)
{
    if (g_inited) {
        return;
    }
    MinList_Init(&g_runq);
    MinList_Init(&g_sleepers);
    MinList_Init(&g_dead);

    struct Task *boot = (struct Task *)Croi_Alloc(sizeof(struct Task));
    if (!boot) {
        LOG_FATAL("schd", "Sched_Init: heap alloc failed");
        return;
    }
    *boot = (struct Task){ 0 };
    task_init_node(boot, "kmain", 100);
    boot->tc_State = TS_RUN;
    if (HandleTable_Init(&boot->handles, CARA_TASK_HANDLE_TABLE_CAP)
        != CARA_EOK) {
        LOG_FATAL("schd", "Sched_Init: HandleTable_Init failed");
        return;
    }
    g_current = boot;
    g_inited = true;
    LOG_INFO("schd", "scheduler initialised; kmain pri=100");
}

struct Task *Sched_Current(void)
{
    return g_current;
}

[[nodiscard]] struct Task *Croi_SpawnKernelTask(const char *name, i32 pri,
                                                KernelTaskFn entry, void *arg)
{
    if (!g_inited || !entry) {
        return nullptr;
    }
    struct Task *t = (struct Task *)Croi_Alloc(sizeof(struct Task));
    if (!t) {
        return nullptr;
    }
    *t = (struct Task){ 0 };
    task_init_node(t, name, clamp_pri(pri));
    t->tc_State = TS_READY;
    t->entry_fn = entry;
    t->entry_arg = arg;

    t->kstack = Croi_Alloc(CARA_TASK_KSTACK_SIZE);
    if (!t->kstack) {
        Croi_Free(t);
        return nullptr;
    }
    t->kstack_size = CARA_TASK_KSTACK_SIZE;
    t->tc_SPLower = t->kstack;
    t->tc_SPUpper = (u8 *)t->kstack + t->kstack_size;
    if (HandleTable_Init(&t->handles, CARA_TASK_HANDLE_TABLE_CAP) != CARA_EOK) {
        Croi_Free(t->kstack);
        Croi_Free(t);
        return nullptr;
    }

    // saved_regs layout: [0]=ra, [1]=sp, [2]=gp, [3]=tp, [4..15]=s0..s11
    u64 sp_top = (u64)(uptr)t->kstack + CARA_TASK_KSTACK_SIZE;
    sp_top &= ~15ull;                       // 16-byte aligned
    t->saved_regs[0] = (u64)(uptr)task_trampoline;
    t->saved_regs[1] = sp_top;
    // gp/tp left zero; s-regs left zero.

    runq_add(t);
    LOG_DEBUG("schd", "spawn '%s' pri=%d kstack=0x%llx",
              t->tc_Node.ln_Name, (int)t->tc_Node.ln_Pri, (u64)(uptr)t->kstack);
    return t;
}

void Croi_Yield(void)
{
    if (!g_inited) {
        return;
    }
    struct Task *old = g_current;
    if (MinList_IsEmpty(&g_runq)) {
        return;     // No-one else ready; current keeps running.
    }
    // Peek at head: if its priority is below the current's, no switch.
    struct Task *head = node_to_task(g_runq.mlh_Head);
    if (old && head->tc_Node.ln_Pri < old->tc_Node.ln_Pri) {
        return;
    }

    struct MinNode *n = MinList_RemHead(&g_runq);
    struct Task *next = node_to_task(n);
    next->tc_State = TS_RUN;

    if (old) {
        old->tc_State = TS_READY;
        runq_add(old);
    }
    g_current = next;
    croi_ctx_switch(old ? old->saved_regs : nullptr, next->saved_regs);
}

[[noreturn]] void Croi_TaskExit(void)
{
    struct Task *old = g_current;
    if (!old) {
        for (;;) {
            __asm__ volatile("wfi");
        }
    }
    old->tc_State = TS_REMOVED;
    LOG_DEBUG("schd", "task '%s' exit", old->tc_Node.ln_Name);
    MinList_AddTail(&g_dead, &old->sched_node);

    if (MinList_IsEmpty(&g_runq)) {
        LOG_INFO("schd", "no runnable tasks; halting");
        for (;;) {
            __asm__ volatile("wfi");
        }
    }
    struct MinNode *n = MinList_RemHead(&g_runq);
    struct Task *next = node_to_task(n);
    next->tc_State = TS_RUN;
    g_current = next;

    // We pass the dying task's saved_regs as the from buffer; ctx_switch
    // will write into it but no one reads it again.
    croi_ctx_switch(old->saved_regs, next->saved_regs);
    __builtin_unreachable();
}

void Croi_TaskSetSelfPriority(i32 pri)
{
    if (g_current) {
        g_current->tc_Node.ln_Pri = clamp_pri(pri);
    }
}

[[nodiscard]] struct Task *Croi_SpawnUserTask(const char *name, i32 pri,
                                              const void *user_text_kva,
                                              usize user_text_size,
                                              u64 user_entry_va)
{
    if (!g_inited || !user_text_kva || user_text_size == 0) {
        return nullptr;
    }
    struct Task *t = (struct Task *)Croi_Alloc(sizeof(struct Task));
    if (!t) {
        return nullptr;
    }
    *t = (struct Task){ 0 };
    task_init_node(t, name, clamp_pri(pri));
    t->tc_State = TS_READY;

    t->kstack = Croi_Alloc(CARA_TASK_KSTACK_SIZE);
    if (!t->kstack) {
        Croi_Free(t);
        return nullptr;
    }
    t->kstack_size = CARA_TASK_KSTACK_SIZE;
    t->tc_SPLower = t->kstack;
    t->tc_SPUpper = (u8 *)t->kstack + t->kstack_size;

    if (HandleTable_Init(&t->handles, CARA_TASK_HANDLE_TABLE_CAP) != CARA_EOK) {
        Croi_Free(t->kstack);
        Croi_Free(t);
        return nullptr;
    }

    // ---- Build the user page table ----
    t->user_pt = Croi_NewKernelPT();
    if (!t->user_pt) {
        HandleTable_Destroy(&t->handles);
        Croi_Free(t->kstack);
        Croi_Free(t);
        return nullptr;
    }

    // Map the user text bytes (already loaded as part of croi.elf's
    // rodata) at the user-VA entry as PTE_USER_RX, page by page.
    u64 base_kva = (u64)(uptr)user_text_kva;
    if (base_kva & (CARA_PAGE_SIZE - 1)) {
        base_kva &= ~(CARA_PAGE_SIZE - 1);     // drop into containing page
    }
    u64 last_byte_kva = (u64)(uptr)user_text_kva + user_text_size - 1;
    u32 n_text_pages = (u32)(((last_byte_kva | (CARA_PAGE_SIZE - 1)) + 1
                              - base_kva)
                             / CARA_PAGE_SIZE);
    for (u32 i = 0; i < n_text_pages; i++) {
        u64 src_pa = Mm_VirtToPhys((const void *)(uptr)(base_kva
                                                        + (u64)i * CARA_PAGE_SIZE));
        u64 dst_va = CARA_USER_TEXT_BASE + (u64)i * CARA_PAGE_SIZE;
        if (Page_Map(t->user_pt, dst_va, src_pa,
                     PTE_V | PTE_R | PTE_X | PTE_U | PTE_A | PTE_D)
            != CARA_EOK) {
            // Failed mid-mapping; leak the partially-built PT for now.
            return nullptr;
        }
    }

    // Allocate + map the user stack.
    extern struct PageAllocator g_page_alloc;
    u32 stack_pages = CARA_USER_STACK_SIZE / (u32)CARA_PAGE_SIZE;
    for (u32 i = 0; i < stack_pages; i++) {
        u64 phys = Page_Alloc(&g_page_alloc, 1);
        if (phys == 0) {
            return nullptr;
        }
        u64 dst_va = CARA_USER_STACK_BASE + (u64)i * CARA_PAGE_SIZE;
        if (Page_Map(t->user_pt, dst_va, phys, PTE_USER_RW) != CARA_EOK) {
            return nullptr;
        }
    }

    t->user_entry = user_entry_va;
    t->user_sp_top = (CARA_USER_STACK_BASE + CARA_USER_STACK_SIZE) & ~15ull;

    // Initial S-mode context: ra = user trampoline; sp = kstack top;
    // sscratch = kstack top (sscratch convention for U-mode tasks so
    // that a user-origin trap swaps onto the kernel stack).
    u64 sp_top = (u64)(uptr)t->kstack + CARA_TASK_KSTACK_SIZE;
    sp_top &= ~15ull;
    t->saved_regs[0] = (u64)(uptr)user_task_trampoline;
    t->saved_regs[1] = sp_top;
    t->saved_regs[16] = sp_top;     // sscratch

    runq_add(t);
    LOG_DEBUG("schd", "spawn user '%s' pri=%d entry=0x%llx sp_top=0x%llx",
              t->tc_Node.ln_Name, (int)t->tc_Node.ln_Pri,
              t->user_entry, t->user_sp_top);
    return t;
}

[[nodiscard]] struct Task *Croi_SpawnUserTaskFromElf(const char *name, i32 pri,
                                                     const void *elf_blob,
                                                     usize elf_size)
{
    if (!g_inited || !elf_blob || elf_size == 0) {
        return nullptr;
    }
    struct Task *t = (struct Task *)Croi_Alloc(sizeof(struct Task));
    if (!t) {
        return nullptr;
    }
    *t = (struct Task){ 0 };
    task_init_node(t, name, clamp_pri(pri));
    t->tc_State = TS_READY;

    t->kstack = Croi_Alloc(CARA_TASK_KSTACK_SIZE);
    if (!t->kstack) {
        Croi_Free(t);
        return nullptr;
    }
    t->kstack_size = CARA_TASK_KSTACK_SIZE;
    t->tc_SPLower = t->kstack;
    t->tc_SPUpper = (u8 *)t->kstack + t->kstack_size;

    if (HandleTable_Init(&t->handles, CARA_TASK_HANDLE_TABLE_CAP) != CARA_EOK) {
        Croi_Free(t->kstack);
        Croi_Free(t);
        return nullptr;
    }

    t->user_pt = Croi_NewKernelPT();
    if (!t->user_pt) {
        HandleTable_Destroy(&t->handles);
        Croi_Free(t->kstack);
        Croi_Free(t);
        return nullptr;
    }

    u64 entry_va = 0;
    int rc = Croi_LoadElf(elf_blob, elf_size, t->user_pt, &entry_va);
    if (rc != CARA_EOK) {
        Croi_DestroyPT(t->user_pt);
        HandleTable_Destroy(&t->handles);
        Croi_Free(t->kstack);
        Croi_Free(t);
        return nullptr;
    }

    extern struct PageAllocator g_page_alloc;
    u32 stack_pages = CARA_USER_STACK_SIZE / (u32)CARA_PAGE_SIZE;
    for (u32 i = 0; i < stack_pages; i++) {
        u64 phys = Page_Alloc(&g_page_alloc, 1);
        if (phys == 0) {
            return nullptr;     // partial state leaks; OK for tests
        }
        u64 dst_va = CARA_USER_STACK_BASE + (u64)i * CARA_PAGE_SIZE;
        if (Page_Map(t->user_pt, dst_va, phys, PTE_USER_RW) != CARA_EOK) {
            return nullptr;
        }
    }

    t->user_entry = entry_va;
    t->user_sp_top = (CARA_USER_STACK_BASE + CARA_USER_STACK_SIZE) & ~15ull;

    u64 sp_top = (u64)(uptr)t->kstack + CARA_TASK_KSTACK_SIZE;
    sp_top &= ~15ull;
    t->saved_regs[0] = (u64)(uptr)user_task_trampoline;
    t->saved_regs[1] = sp_top;
    t->saved_regs[16] = sp_top;     // sscratch

    runq_add(t);
    LOG_DEBUG("schd", "spawn user-elf '%s' pri=%d entry=0x%llx sp_top=0x%llx",
              t->tc_Node.ln_Name, (int)t->tc_Node.ln_Pri,
              t->user_entry, t->user_sp_top);
    return t;
}

// Called from task_trampoline (.S) on the new task's first dispatch.
void Sched_Trampoline(void)
{
    struct Task *t = g_current;
    if (t && t->entry_fn) {
        t->entry_fn(t->entry_arg);
    }
    Croi_TaskExit();
}

void Sched_ReapDead(void)
{
    while (!MinList_IsEmpty(&g_dead)) {
        struct MinNode *n = MinList_RemHead(&g_dead);
        struct Task *t = node_to_task(n);
        if (t->kstack) {
            Croi_Free(t->kstack);
            t->kstack = nullptr;
        }
        HandleTable_Destroy(&t->handles);
        Croi_Free(t);
    }
}

// ---- Signals ------------------------------------------------------------

[[nodiscard]] i32 Croi_AllocSignal(void)
{
    if (!g_current) {
        return -1;
    }
    for (u32 b = 0; b < 32; b++) {
        u32 mask = 1u << b;
        if (!(g_current->tc_SigAlloc & mask)) {
            g_current->tc_SigAlloc |= mask;
            g_current->tc_SigRecvd &= ~mask;     // start clean
            return (i32)b;
        }
    }
    return -1;
}

void Croi_FreeSignal(i32 sig)
{
    if (sig < 0 || sig >= 32 || !g_current) {
        return;
    }
    u32 mask = 1u << (u32)sig;
    g_current->tc_SigAlloc &= ~mask;
    g_current->tc_SigRecvd &= ~mask;
}

void Croi_Signal(struct Task *target, u32 mask)
{
    if (!target || mask == 0) {
        return;
    }
    target->tc_SigRecvd |= mask;
    if (target->tc_State == TS_WAIT
        && (target->tc_SigRecvd & target->tc_SigWait) != 0) {
        // Move from sleepers to runq.
        MinList_Remove(&target->sched_node);
        target->tc_State = TS_READY;
        target->tc_SigWait = 0;
        runq_add(target);
    }
}

u32 Croi_Wait(u32 mask)
{
    if (!g_current || mask == 0) {
        return 0;
    }
    while (true) {
        u32 hit = g_current->tc_SigRecvd & mask;
        if (hit != 0) {
            g_current->tc_SigRecvd &= ~hit;
            g_current->tc_SigWait = 0;
            return hit;
        }

        // Block: move self off the run queue and switch to whoever's next.
        g_current->tc_SigWait = mask;
        g_current->tc_State = TS_WAIT;

        if (MinList_IsEmpty(&g_runq)) {
            // No one to switch to. With Tier 1's setup that's usually a
            // bug — Wait without a possible signaler is a deadlock — but
            // we tolerate it by spinning in WFI; an external interrupt
            // (timer) will eventually wake us via the trap path once
            // those land. For now, panic.
            LOG_FATAL("schd",
                      "Croi_Wait by '%s' with empty runq (deadlock)",
                      g_current->tc_Node.ln_Name);
            for (;;) {
                __asm__ volatile("wfi");
            }
        }
        struct Task *old = g_current;
        MinList_AddTail(&g_sleepers, &old->sched_node);

        struct MinNode *n = MinList_RemHead(&g_runq);
        struct Task *next = node_to_task(n);
        next->tc_State = TS_RUN;
        g_current = next;
        croi_ctx_switch(old->saved_regs, next->saved_regs);
        // resume here when re-scheduled — loop and re-check sigrecvd
    }
}
