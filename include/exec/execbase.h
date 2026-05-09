// SPDX-License-Identifier: BSD-2-Clause
//
// AmigaOS V36+ struct ExecBase — the library base of exec.library.
//
// V36+ exposes ExecBase as the typed view of SysBase. Phase C minimum
// is the LibNode prefix so user code can dereference SysBase to read
// lib_Version / lib_Revision / etc. (the §9 worked example pattern).
// Additional V36+ fields — ThisTask, ResModules, IntVects, MemList,
// LibList, PortList, ColdCapture, etc. — are declared as forward
// extensions; their concrete semantics arrive incrementally as Phase 3
// libraries that consume them land.
//
// Layout: the LibNode field at offset 0 is V36+ canonical. Fields past
// LibNode are V36+-named placeholders; their internal binding is up to
// CaraOS. v0 keeps them zero-init.

#ifndef EXEC_EXECBASE_H
#define EXEC_EXECBASE_H

#include <exec/libraries.h>
#include <exec/lists.h>
#include <exec/nodes.h>
#include <exec/types.h>

struct Task; // <exec/tasks.h> defines this

struct ExecBase {
    // ---- V36+ public ABI: LibNode prefix --------------------------------
    struct Library LibNode;

    // ---- V36+ scalar bookkeeping -- placeholder, zero-init in v0 -------
    UWORD SoftVer;
    WORD LowMemChkSum;
    ULONG ChkBase;
    APTR ColdCapture;
    APTR CoolCapture;
    APTR WarmCapture;
    APTR SysStkUpper;
    APTR SysStkLower;
    ULONG MaxLocMem;
    APTR DebugEntry;
    APTR DebugData;
    APTR AlertData;
    APTR MaxExtMem;
    UWORD ChkSum;

    // ---- Scheduler-visible state ----------------------------------------
    struct Task *ThisTask; // populated by trap entry per-hart in Phase 4
    ULONG IdleCount;
    ULONG DispCount;
    UWORD Quantum;
    UWORD Elapsed;
    UWORD SysFlags;
    BYTE IDNestCnt;
    BYTE TDNestCnt;
    UWORD AttnFlags;
    UWORD AttnResched;

    // ---- Resident-module / handler hooks --------------------------------
    APTR ResModules;
    APTR TaskTrapCode;
    APTR TaskExceptCode;
    APTR TaskExitCode;
    ULONG TaskSigAlloc;
    UWORD TaskTrapAlloc;

    // ---- Lists ----------------------------------------------------------
    struct List MemList;
    struct List ResourceList;
    struct List DeviceList;
    struct List IntrList;
    struct List LibList; // mirrors the kernel-internal registry
    struct List PortList;
    struct List TaskReady;
    struct List TaskWait;

    // ---- Misc V37+ extensions -- zero-init in v0 -----------------------
    LONG LastAlert[4];
    UBYTE VBlankFrequency;
    UBYTE PowerSupplyFrequency;
    struct List SemaphoreList;
    APTR KickMemPtr;
    APTR KickTagPtr;
    APTR KickCheckSum;
    UWORD ex_Pad0;
    ULONG ex_LaunchPoint;
    APTR ex_RamLibPrivate;
    ULONG ex_EClockFrequency;
    ULONG ex_CacheControl;
    ULONG ex_TaskID;
    ULONG ex_Reserved1[5];
    APTR ex_MMULock;
    ULONG ex_Reserved2[3];
    struct MinList ex_MemHandlers;
    APTR ex_MemHandler;
};

#endif
