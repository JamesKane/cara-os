// SPDX-License-Identifier: BSD-2-Clause
//
// Cooperative kernel scheduler. A single priority-sorted run queue;
// Sched_Current points at the running task; Croi_Yield is the only
// switch trigger. Single hart for now (SMP arrives in Epic H).

#include <cara/alloc.h>
#include <cara/list.h>
#include <cara/log.h>
#include <cara/sched.h>
#include <cara/types.h>

extern void croi_ctx_switch(u64 *from, u64 *to);
extern void task_trampoline(void);
void Sched_Trampoline(void);

static struct Task    *g_current = nullptr;
static struct MinList  g_runq;
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

// Called from task_trampoline (.S) on the new task's first dispatch.
void Sched_Trampoline(void)
{
    struct Task *t = g_current;
    if (t && t->entry_fn) {
        t->entry_fn(t->entry_arg);
    }
    Croi_TaskExit();
}
