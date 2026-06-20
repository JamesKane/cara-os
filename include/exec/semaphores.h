// SPDX-License-Identifier: BSD-2-Clause
//
// V36+ exec.library signal semaphores. Verbatim AmigaOS ABI: the struct
// layout is what InitSemaphore/ObtainSemaphore/ReleaseSemaphore/
// AttemptSemaphore (and any program that pokes ss_NestCount / ss_Owner)
// expect. A SignalSemaphore is caller-owned memory; under SASOS it is a
// valid pointer in every task, so the kernel manipulates the same
// structure the program sees.

#ifndef EXEC_SEMAPHORES_H
#define EXEC_SEMAPHORES_H

#include <exec/lists.h>
#include <exec/nodes.h>
#include <exec/types.h>

struct Task;

// A waiter's request, queued on a held semaphore. Lives on the waiting
// task's stack while it is blocked (the AmigaOS idiom).
struct SemaphoreRequest {
    struct MinNode ssr_Link;
    struct Task *ssr_Waiter;
};

struct SignalSemaphore {
    struct Node ss_Link;
    WORD ss_NestCount;
    struct MinList ss_WaitQueue;
    struct SemaphoreRequest ss_MultipleLink;
    struct Task *ss_Owner;
    WORD ss_QueueCount;
};

#endif // EXEC_SEMAPHORES_H
