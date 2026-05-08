// SPDX-License-Identifier: BSD-2-Clause
//
// Cooperative kernel scheduler. A single priority-sorted run queue;
// Sched_Current points at the running task; Croi_Yield is the only
// switch trigger. Single hart for now (SMP arrives in Epic H).

#include <cara/alloc.h>
#include <cara/kobj.h>
#include <cara/list.h>
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

static struct Task *node_to_task(struct ListNode *n)
{
    return ListNodeOf(n, struct Task, sched_node);
}

static void name_copy(char *dst, const char *src, usize cap)
{
    usize i = 0;
    if (cap == 0) {
        return;
    }
    while (src && i + 1 < cap && src[i]) {
        dst[i] = src[i];
        i++;
    }
    dst[i] = 0;
}

// Insert t into runq sorted by priority descending. Equal-pri tasks
// land at the tail of their priority chain → round-robin within a
// priority level.
static void runq_add(struct Task *t)
{
    struct ListNode *p = g_runq.head.succ;
    while (p != &g_runq.tail) {
        struct Task *q = node_to_task(p);
        if (q->pri < t->pri) {
            t->sched_node.pred = p->pred;
            t->sched_node.succ = p;
            p->pred->succ = &t->sched_node;
            p->pred = &t->sched_node;
            return;
        }
        p = p->succ;
    }
    ListAddTail(&g_runq, &t->sched_node);
}

void Sched_Init(void)
{
    if (g_inited) {
        return;
    }
    ListInit(&g_runq);
    ListInit(&g_sleepers);
    ListInit(&g_dead);

    struct Task *boot = (struct Task *)Croi_Alloc(sizeof(struct Task));
    if (!boot) {
        LOG_FATAL("schd", "Sched_Init: heap alloc failed");
        return;
    }
    *boot = (struct Task){ 0 };
    name_copy(boot->name, "kmain", sizeof(boot->name));
    boot->pri = 100;
    boot->state = TASK_STATE_RUNNING;
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
    name_copy(t->name, name, sizeof(t->name));
    t->pri = pri;
    t->state = TASK_STATE_READY;
    t->entry_fn = entry;
    t->entry_arg = arg;

    t->kstack = Croi_Alloc(CARA_TASK_KSTACK_SIZE);
    if (!t->kstack) {
        Croi_Free(t);
        return nullptr;
    }
    t->kstack_size = CARA_TASK_KSTACK_SIZE;
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
    LOG_DEBUG("schd", "spawn '%s' pri=%d kstack=0x%llx", t->name, t->pri,
              (u64)(uptr)t->kstack);
    return t;
}

void Croi_Yield(void)
{
    if (!g_inited) {
        return;
    }
    struct Task *old = g_current;
    if (ListIsEmpty(&g_runq)) {
        return;     // No-one else ready; current keeps running.
    }
    // Peek at head: if its priority is below the current's, no switch.
    struct Task *head = node_to_task(g_runq.head.succ);
    if (old && head->pri < old->pri) {
        return;
    }

    struct ListNode *n = ListRemHead(&g_runq);
    struct Task *next = node_to_task(n);
    next->state = TASK_STATE_RUNNING;

    if (old) {
        old->state = TASK_STATE_READY;
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
    old->state = TASK_STATE_DEAD;
    LOG_DEBUG("schd", "task '%s' exit", old->name);
    ListAddTail(&g_dead, &old->sched_node);

    if (ListIsEmpty(&g_runq)) {
        LOG_INFO("schd", "no runnable tasks; halting");
        for (;;) {
            __asm__ volatile("wfi");
        }
    }
    struct ListNode *n = ListRemHead(&g_runq);
    struct Task *next = node_to_task(n);
    next->state = TASK_STATE_RUNNING;
    g_current = next;

    // We pass the dying task's saved_regs as the from buffer; ctx_switch
    // will write into it but no one reads it again.
    croi_ctx_switch(old->saved_regs, next->saved_regs);
    __builtin_unreachable();
}

void Croi_TaskSetSelfPriority(i32 pri)
{
    if (g_current) {
        g_current->pri = pri;
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
    name_copy(t->name, name, sizeof(t->name));
    t->pri = pri;
    t->state = TASK_STATE_READY;

    t->kstack = Croi_Alloc(CARA_TASK_KSTACK_SIZE);
    if (!t->kstack) {
        Croi_Free(t);
        return nullptr;
    }
    t->kstack_size = CARA_TASK_KSTACK_SIZE;

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
              t->name, t->pri, t->user_entry, t->user_sp_top);
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
    while (!ListIsEmpty(&g_dead)) {
        struct ListNode *n = ListRemHead(&g_dead);
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
        if (!(g_current->sigalloc & mask)) {
            g_current->sigalloc |= mask;
            g_current->sigrecvd &= ~mask;     // start clean
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
    g_current->sigalloc &= ~mask;
    g_current->sigrecvd &= ~mask;
}

void Croi_Signal(struct Task *target, u32 mask)
{
    if (!target || mask == 0) {
        return;
    }
    target->sigrecvd |= mask;
    if (target->state == TASK_STATE_BLOCKED
        && (target->sigrecvd & target->sigwait) != 0) {
        // Move from sleepers to runq.
        ListRemove(&target->sched_node);
        target->state = TASK_STATE_READY;
        target->sigwait = 0;
        runq_add(target);
    }
}

u32 Croi_Wait(u32 mask)
{
    if (!g_current || mask == 0) {
        return 0;
    }
    while (true) {
        u32 hit = g_current->sigrecvd & mask;
        if (hit != 0) {
            g_current->sigrecvd &= ~hit;
            g_current->sigwait = 0;
            return hit;
        }

        // Block: move self off the run queue and switch to whoever's next.
        g_current->sigwait = mask;
        g_current->state = TASK_STATE_BLOCKED;

        if (ListIsEmpty(&g_runq)) {
            // No one to switch to. With Tier 1's setup that's usually a
            // bug — Wait without a possible signaler is a deadlock — but
            // we tolerate it by spinning in WFI; an external interrupt
            // (timer) will eventually wake us via the trap path once
            // those land. For now, panic.
            LOG_FATAL("schd",
                      "Croi_Wait by '%s' with empty runq (deadlock)",
                      g_current->name);
            for (;;) {
                __asm__ volatile("wfi");
            }
        }
        struct Task *old = g_current;
        ListAddTail(&g_sleepers, &old->sched_node);

        struct ListNode *n = ListRemHead(&g_runq);
        struct Task *next = node_to_task(n);
        next->state = TASK_STATE_RUNNING;
        g_current = next;
        croi_ctx_switch(old->saved_regs, next->saved_regs);
        // resume here when re-scheduled — loop and re-check sigrecvd
    }
}
