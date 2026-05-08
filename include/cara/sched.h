// SPDX-License-Identifier: BSD-2-Clause
//
// Cooperative kernel scheduler.
//
// struct Task itself is the V36+ public type from <exec/tasks.h>;
// CaraOS's kernel-private fields live appended after tc_UserData in
// the same struct (per DRIFT_2026-05.md H5). This header just adds
// brand-namespace scheduler operations — Croi_SpawnKernelTask,
// Croi_SpawnUserTask, Croi_Yield, Croi_TaskExit — plus the signal
// helpers (Croi_AllocSignal / Croi_FreeSignal / Croi_Signal /
// Croi_Wait) that exec.library's Wait / Signal / AllocSignal /
// FreeSignal LVOs trampoline into in Phase C.

#ifndef CARA_SCHED_H
#define CARA_SCHED_H

#include <cara/kobj.h>
#include <cara/list.h>
#include <cara/mm.h>
#include <cara/types.h>
#include <exec/tasks.h>

// User VA of the embedded user program's entry / stack.
//   TEXT_BASE — small; kept clear of the null guard (0x0..0x100000)
//   STACK_BASE — in the SASOS per-task-private-slab range
//                (ARCHITECTURE.md §4.3, 0x11_0000_0000 + 176 GiB).
//                Must NOT collide with the shared-library region
//                (0x4000_0000–0x4_4000_0000) or the shared heap.
// Later phases let user code pick its own layout via the ELF.
#define CARA_USER_TEXT_BASE   0x0000000000010000ull
#define CARA_USER_STACK_BASE  0x0000001100000000ull
#define CARA_USER_STACK_SIZE  (4u * 4096u)

typedef void (*KernelTaskFn)(void *arg);

// Bootstrap. Allocates the kmain Task struct from the heap and binds
// the boot context to it (current = kmain). Must run after Heap_Init.
void Sched_Init(void);

[[nodiscard]] struct Task *Croi_SpawnKernelTask(const char *name, i32 pri,
                                                KernelTaskFn entry, void *arg);

[[nodiscard]] struct Task *Croi_SpawnUserTask(const char *name, i32 pri,
                                              const void *user_text_kva,
                                              usize user_text_size,
                                              u64 user_entry_va);

[[nodiscard]] struct Task *Croi_SpawnUserTaskFromElf(const char *name, i32 pri,
                                                     const void *elf_blob,
                                                     usize elf_size);

void Croi_Yield(void);

[[noreturn]] void Croi_TaskExit(void);

void Croi_TaskSetSelfPriority(i32 pri);

struct Task *Sched_Current(void);

void Sched_ReapDead(void);

// ---- Signals (Exec-style) -------------------------------------------------

[[nodiscard]] i32 Croi_AllocSignal(void);
void Croi_FreeSignal(i32 sig);
void Croi_Signal(struct Task *target, u32 mask);
u32 Croi_Wait(u32 mask);

// V36+ SetSignal: atomically replaces the bits of the current task's
// tc_SigRecvd specified by `mask` with the corresponding bits from
// `new_signals`; returns the previous tc_SigRecvd masked by `mask`.
// Bits outside `mask` are unchanged.
u32 Croi_SetSignal(u32 new_signals, u32 mask);

#endif
