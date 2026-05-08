// SPDX-License-Identifier: BSD-2-Clause
//
// Cooperative kernel scheduler. Tier 2 cut: priority-sorted run queue,
// 16-register saved context (ra/sp/gp/tp/s0..s11), Croi_Yield as the
// only switch trigger. Preemption via timer arrives in a later slice
// once we're sure cooperative semantics behave.

#ifndef CARA_SCHED_H
#define CARA_SCHED_H

#include <cara/list.h>
#include <cara/types.h>

#define CARA_TASK_NAME_LEN 16
#define CARA_TASK_KSTACK_SIZE 16384u

typedef enum : u32 {
    TASK_STATE_READY    = 1,
    TASK_STATE_RUNNING  = 2,
    TASK_STATE_DEAD     = 3,
} TaskState;

// 16 callee-saved registers we round-trip across a voluntary yield:
//   [0]=ra, [1]=sp, [2]=gp, [3]=tp, [4..15]=s0..s11.
// Caller-saved registers are clobbered by the call to croi_ctx_switch
// itself, so the compiler already saves them as needed.
#define TASK_NSAVED 16

struct Task {
    char            name[CARA_TASK_NAME_LEN];
    i32             pri;             // -128..127, larger = higher priority
    TaskState       state;
    u64             saved_regs[TASK_NSAVED];
    void           *kstack;          // base of stack region
    usize           kstack_size;
    void          (*entry_fn)(void *);
    void           *entry_arg;
    struct ListNode sched_node;
};

typedef void (*KernelTaskFn)(void *arg);

// Bootstrap. Allocates the kmain Task struct from the heap and binds
// the boot context to it (current = kmain). Must run after Heap_Init.
void Sched_Init(void);

[[nodiscard]] struct Task *Croi_SpawnKernelTask(const char *name, i32 pri,
                                                KernelTaskFn entry, void *arg);

// Yield the CPU. Picks the highest-priority READY task from the run
// queue; if every other ready task has lower priority than the
// current one, no switch happens and the call returns immediately.
void Croi_Yield(void);

// Terminate the current task. Switches to the next runnable task and
// never returns. The caller's Task struct is moved to a dead list and
// reaped lazily.
[[noreturn]] void Croi_TaskExit(void);

// Adjust the priority of the current task. Used by tests that need to
// drop their priority temporarily so lower-priority workers run.
void Croi_TaskSetSelfPriority(i32 pri);

struct Task *Sched_Current(void);

#endif
