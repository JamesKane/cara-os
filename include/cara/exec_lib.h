// SPDX-License-Identifier: BSD-2-Clause
//
// Kernel-side impl prototypes for the V36+ exec.library LVOs that
// Croi serves as `syscall` flavour. Each impl below is what
// src/croi/syscall/syscall.c routes to when the matching SYS_* trap
// arrives — the per-LVO trampoline (Cara_Trampoline_<Name> in
// src/croi/exec_lib/trampolines.S) just does `li a7, <num>; ecall;
// ret`, so the impl signature here is the V36+ canonical signature
// minus the trailing `struct Library *base` (CaraOS's syscall path
// doesn't need the base; the kernel knows SysBase).

#ifndef CARA_EXEC_LIB_H
#define CARA_EXEC_LIB_H

#include <cara/types.h>
#include <exec/memory.h>
#include <exec/ports.h>
#include <exec/tasks.h>
#include <exec/types.h>

// ---- Memory ---------------------------------------------------------

APTR Croi_AllocMem_Impl(ULONG size, ULONG flags);
void Croi_FreeMem_Impl(APTR addr, ULONG size);
APTR Croi_AllocVec_Impl(ULONG size, ULONG flags);
void Croi_FreeVec_Impl(APTR mem);

// ---- Signals --------------------------------------------------------

ULONG Croi_Wait_Impl(ULONG mask);
void Croi_Signal_Impl(struct Task *target, ULONG mask);
LONG Croi_AllocSignal_Impl(LONG sig_num);
void Croi_FreeSignal_Impl(LONG sig_num);
ULONG Croi_SetSignal_Impl(ULONG new_signals, ULONG mask);
struct Task *Croi_FindTask_Impl(STRPTR name);

// ---- Task-switch control --------------------------------------------

void Croi_Forbid_Impl(void);
void Croi_Permit_Impl(void);
void Croi_Disable_Impl(void);
void Croi_Enable_Impl(void);

// ---- IPC ------------------------------------------------------------

void Croi_PutMsg_Impl(struct MsgPort *port, struct Message *msg);
struct Message *Croi_GetMsg_Impl(struct MsgPort *port);
struct Message *Croi_WaitPort_Impl(struct MsgPort *port);
void Croi_ReplyMsg_Impl(struct Message *msg);
struct MsgPort *Croi_CreateMsgPort_Impl(void);
void Croi_DeleteMsgPort_Impl(struct MsgPort *port);

// ---- Semaphores -----------------------------------------------------

struct SignalSemaphore;
void Croi_InitSemaphore_Impl(struct SignalSemaphore *sem);
void Croi_ObtainSemaphore_Impl(struct SignalSemaphore *sem);
void Croi_ReleaseSemaphore_Impl(struct SignalSemaphore *sem);
BOOL Croi_AttemptSemaphore_Impl(struct SignalSemaphore *sem);

// ---- Library lifecycle ----------------------------------------------

struct Library;
struct TagItem;

// Construct a library from a MakeLibrary TagItem array (see
// <cara/makelibrary.h> for the MKL_* tag IDs). Returns the libBase
// pointer on success and registers the library in the kernel name
// list; returns nullptr on failure.
struct Library *Croi_MakeLibrary(const struct TagItem *tags);

// V36+ OpenLibrary / OldOpenLibrary / CloseLibrary syscall impls.
struct Library *Croi_OpenLibrary_Impl(STRPTR name, ULONG version);
struct Library *Croi_OldOpenLibrary_Impl(STRPTR name);
void Croi_CloseLibrary_Impl(struct Library *lib);

#endif
