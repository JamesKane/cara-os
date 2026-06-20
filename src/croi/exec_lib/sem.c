// SPDX-License-Identifier: BSD-2-Clause
//
// V36+ exec.library signal semaphores (Phase 3 L1). The SignalSemaphore
// is caller-owned SASOS memory, so the kernel manipulates the same
// struct the program sees. Single-hart cooperative model:
//
//   - Obtain on a free or self-owned semaphore takes/nests it inline.
//   - Obtain on a contended semaphore queues a SemaphoreRequest (on the
//     waiter's kstack) and blocks on SIGF_SINGLE — the waiter leaves the
//     run queue, so the lower-priority owner gets to run (no priority
//     inversion, which a yield-spin would risk under the strict-priority
//     scheduler).
//   - Release at nest 0 hands the semaphore to the queued waiter and
//     Signals it; else clears the owner.
//
// SIGB_SINGLE (bit 4) is reserved (AllocSignal hands out 16..31), so the
// wait bit never collides with a program's own signals.

#include <cara/exec_lib.h>
#include <cara/list.h>
#include <cara/sched.h>
#include <cara/types.h>
#include <exec/nodes.h>
#include <exec/semaphores.h>
#include <exec/tasks.h>
#include <exec/types.h>

void Croi_InitSemaphore_Impl(struct SignalSemaphore *sem)
{
    if (!sem) {
        return;
    }
    sem->ss_Link.ln_Type = NT_SIGNALSEM;
    sem->ss_Link.ln_Pri = 0;
    sem->ss_Link.ln_Name = nullptr;
    sem->ss_NestCount = 0;
    sem->ss_QueueCount = -1;
    sem->ss_Owner = nullptr;
    MinList_Init(&sem->ss_WaitQueue);
}

void Croi_ObtainSemaphore_Impl(struct SignalSemaphore *sem)
{
    if (!sem) {
        return;
    }
    struct Task *me = Sched_Current();
    if (sem->ss_Owner == nullptr) {
        sem->ss_Owner = me;
        sem->ss_NestCount = 1;
        return;
    }
    if (sem->ss_Owner == me) {
        sem->ss_NestCount++;
        return;
    }
    // Contended: queue + block. The request lives on our kstack, valid
    // while we're blocked; the releaser pops it and signals us.
    struct SemaphoreRequest req;
    req.ssr_Waiter = me;
    MinList_AddTail(&sem->ss_WaitQueue, &req.ssr_Link);
    sem->ss_QueueCount++;
    (void)Croi_SetSignal(0, (u32)SIGF_SINGLE); // clear any stale single-shot
    (void)Croi_Wait((u32)SIGF_SINGLE);
    // On wake the releaser has set ss_Owner = me, ss_NestCount = 1, and
    // already removed our request from the queue.
}

void Croi_ReleaseSemaphore_Impl(struct SignalSemaphore *sem)
{
    if (!sem) {
        return;
    }
    if (sem->ss_NestCount > 0) {
        sem->ss_NestCount--;
    }
    if (sem->ss_NestCount != 0) {
        return; // still nested by the owner
    }
    struct MinNode *n = MinList_RemHead(&sem->ss_WaitQueue);
    if (n) {
        struct SemaphoreRequest *req = (struct SemaphoreRequest *)n; // ssr_Link first
        sem->ss_QueueCount--;
        sem->ss_Owner = req->ssr_Waiter;
        sem->ss_NestCount = 1;
        Croi_Signal(req->ssr_Waiter, (u32)SIGF_SINGLE);
    } else {
        sem->ss_Owner = nullptr;
    }
}

BOOL Croi_AttemptSemaphore_Impl(struct SignalSemaphore *sem)
{
    if (!sem) {
        return FALSE;
    }
    struct Task *me = Sched_Current();
    if (sem->ss_Owner == nullptr) {
        sem->ss_Owner = me;
        sem->ss_NestCount = 1;
        return TRUE;
    }
    if (sem->ss_Owner == me) {
        sem->ss_NestCount++;
        return TRUE;
    }
    return FALSE;
}
