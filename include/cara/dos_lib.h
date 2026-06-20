// SPDX-License-Identifier: BSD-2-Clause
//
// Kernel-side impl prototypes for the `syscall`-flavour dos.library
// LVOs. (The packet ops — Open/Read/Lock/Examine/… — are `server`
// flavour and run in the Logaic dos handler Gleas, not here; they arrive
// with later L3 slices.) Routed from src/croi/syscall/syscall.c via the
// per-LVO trampoline in src/logaic/dos/trampolines.S.

#ifndef CARA_DOS_LIB_H
#define CARA_DOS_LIB_H

#include <cara/types.h>
#include <dos/dos.h> // BPTR
#include <exec/types.h>

struct MsgPort;
struct DosPacket;

// IoErr — the calling Process's pr_Result2. The kernel reads
// Sched_Current() and casts to struct Process * (every U-mode Gleas's
// Task is embedded at the front of a shared-heap Process; see
// docs/LOGAIC_DOS.md §2.2).
LONG Croi_Dos_IoErr_Impl(void);

// Spawn the dos handler server task (a kernel-resident server behind
// dos.library's packet ops — docs/LOGAIC_DOS.md §2.3). Call once at boot
// after Sched_Init; it yields so the handler publishes its port before
// returning.
void Croi_Dos_StartHandler(void);

// The handler's request port (nullptr until the handler is up). U-mode
// server stubs reach it via SYS_Dos_HandlerPort.
struct MsgPort *Croi_Dos_HandlerPort_Impl(void);

// Shared packet dispatch — runs a DosPacket's ACTION_* against the
// CaraFS mount. Called by the handler task and by the lock/file LVO
// impls' fast path.
void Croi_Dos_Dispatch(struct DosPacket *dp);

// ---- Lock LVOs (L3.3) -----------------------------------------------
struct FileInfoBlock;
BPTR Croi_Dos_Lock_Impl(STRPTR name, LONG accessMode);
void Croi_Dos_UnLock_Impl(BPTR lock);
BPTR Croi_Dos_DupLock_Impl(BPTR lock);
BPTR Croi_Dos_CurrentDir_Impl(BPTR lock);
LONG Croi_Dos_Examine_Impl(BPTR lock, struct FileInfoBlock *fib);
LONG Croi_Dos_ExNext_Impl(BPTR lock, struct FileInfoBlock *fib);

// ---- File I/O LVOs (L3.4) -------------------------------------------
BPTR Croi_Dos_Open_Impl(STRPTR name, LONG accessMode);
LONG Croi_Dos_Close_Impl(BPTR file);
LONG Croi_Dos_Read_Impl(BPTR file, APTR buffer, LONG length);
LONG Croi_Dos_Write_Impl(BPTR file, APTR buffer, LONG length);
LONG Croi_Dos_Seek_Impl(BPTR file, LONG position, LONG mode);

// ---- Mutation + info LVOs (L3.5) ------------------------------------
struct InfoData;
LONG Croi_Dos_DeleteFile_Impl(STRPTR name);
LONG Croi_Dos_Rename_Impl(STRPTR oldName, STRPTR newName);
LONG Croi_Dos_Info_Impl(BPTR lock, struct InfoData *parameterBlock);
BPTR Croi_Dos_CreateDir_Impl(STRPTR name);

#endif // CARA_DOS_LIB_H
