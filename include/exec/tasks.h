// SPDX-License-Identifier: BSD-2-Clause
//
// AmigaOS V36+ Task — the public part of CaraOS's task object.
//
// V36+ exposes struct Task fields up to and including tc_UserData
// (the documented public ABI). Anything beyond is implementation
// state that V36+ source-level code never references. CaraOS appends
// its kernel-private bookkeeping (saved_regs, kstack, handle table,
// user-page-table pointer, scheduler queue link) after tc_UserData
// in this same struct — DRIFT_2026-05.md H5 recommendation, "the
// same Task object serves both kernel scheduler use and exec.library's
// view, which is how V36+ Exec was actually built."
//
// User programs see this struct through `struct Task *` returned by
// FindTask / used by Wait / Signal / ReplyMsg etc. They never
// allocate one — exec.library's Croi_SpawnKernelTask /
// Croi_SpawnUserTask construct them.

#ifndef EXEC_TASKS_H
#define EXEC_TASKS_H

#include <cara/types.h>          // u64, kernel-private widths
#include <cara/kobj.h>           // struct HandleTable

#include <exec/lists.h>
#include <exec/nodes.h>
#include <exec/types.h>

#define CARA_TASK_NAME_LEN          16
#define CARA_TASK_KSTACK_SIZE       16384u
#define CARA_TASK_HANDLE_TABLE_CAP  32

// 16 callee-saved registers we round-trip across a voluntary yield:
//   [0]=ra, [1]=sp, [2]=gp, [3]=tp, [4..15]=s0..s11
//   [16]=sscratch (kernel-mode invariant 0 for S-mode tasks; user-task
//        kstack_top for U-mode tasks)
#define TASK_NSAVED 17

// V36+ tc_State constants (exec/tasks.i verbatim).
#define TS_INVALID  0
#define TS_ADDED    1
#define TS_RUN      2
#define TS_READY    3
#define TS_WAIT     4
#define TS_EXCEPT   5
#define TS_REMOVED  6

struct PageTable;     // forward — kernel-private, defined in cara/mm.h

struct Task {
    // ---- V36+ public ABI -------------------------------------------------
    struct Node tc_Node;
    UBYTE       tc_Flags;
    UBYTE       tc_State;
    BYTE        tc_IDNestCnt;
    BYTE        tc_TDNestCnt;
    ULONG       tc_SigAlloc;
    ULONG       tc_SigWait;
    ULONG       tc_SigRecvd;
    ULONG       tc_SigExcept;
    UWORD       tc_TrapAlloc;
    UWORD       tc_TrapAble;
    APTR        tc_ExceptData;
    APTR        tc_ExceptCode;
    APTR        tc_TrapData;
    APTR        tc_TrapCode;
    APTR        tc_SPReg;
    APTR        tc_SPLower;
    APTR        tc_SPUpper;
    void      (*tc_Switch)(void);
    void      (*tc_Launch)(void);
    struct List tc_MemEntry;
    APTR        tc_UserData;

    // ---- CaraOS kernel-private state (post-tc_UserData) -----------------
    // Field order is implementation-only; V36+ source must not depend on it.
    char               _ln_name_buf[CARA_TASK_NAME_LEN];   // backs tc_Node.ln_Name
    u64                saved_regs[TASK_NSAVED];
    void              *kstack;
    usize              kstack_size;
    void             (*entry_fn)(void *);
    void              *entry_arg;
    struct HandleTable handles;
    struct PageTable  *user_pt;
    u64                user_entry;
    u64                user_sp_top;
    struct MinNode     sched_node;
};

#endif // EXEC_TASKS_H
