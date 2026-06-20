// SPDX-License-Identifier: BSD-2-Clause
//
// AmigaOS V36+ dos.library internal-but-public structures (3rd Edition
// RKMs / dos/dosextens.h): the Process (which extends exec's Task), the
// DosLibrary base, the packet types, and the lock/filehandle objects.
//
// CaraOS layout note (docs/LOGAIC_DOS.md §2.2): struct Process embeds
// CaraOS's struct Task — which already carries kernel-private fields
// appended after the public tc_UserData (exec/tasks.h). So pr_* offsets
// match *these* headers, not the classic 68k offsets (binary compat is
// the Phase-9 translator's job). A U-mode Gleas's Task is allocated at
// the front of a Process in the SASOS shared heap, so
// (struct Process *)FindTask(NULL) is legal in U-mode.

#ifndef DOS_DOSEXTENS_H
#define DOS_DOSEXTENS_H

#include <dos/dos.h>
#include <exec/libraries.h>
#include <exec/lists.h>
#include <exec/nodes.h>
#include <exec/ports.h>
#include <exec/tasks.h>
#include <exec/types.h>

// A Process is a Task with AmigaDOS context. pr_Task must be first
// (FindTask returns &pr_Task; programs cast the result to Process *).
struct Process {
    struct Task pr_Task;
    struct MsgPort pr_MsgPort; // packets to this process arrive here
    WORD pr_Pad;
    BPTR pr_SegList;
    LONG pr_StackSize;
    APTR pr_GlobVec;
    LONG pr_TaskNum; // CLI task number, 0 if not a CLI
    BPTR pr_StackBase;
    LONG pr_Result2;    // the IoErr() value
    BPTR pr_CurrentDir; // lock on the current directory
    BPTR pr_CIS;        // console input stream (FileHandle BPTR)
    BPTR pr_COS;        // console output stream
    APTR pr_ConsoleTask;
    APTR pr_FileSystemTask;
    BPTR pr_CLI;
    APTR pr_ReturnAddr;
    APTR pr_PktWait;
    APTR pr_WindowPtr;
    // ---- V36+ additions ----
    BPTR pr_HomeDir;
    LONG pr_Flags;
    void (*pr_ExitCode)(void);
    LONG pr_ExitData;
    UBYTE *pr_Arguments;
    struct MinList pr_LocalVars;
    ULONG pr_ShellPrivate;
    BPTR pr_CES; // console error stream
};

// The dos.library base.
struct DosLibrary {
    struct Library dl_lib;
    APTR dl_Root; // RootNode (unused in v0)
    APTR dl_GV;   // BCPL global vector (unused)
    LONG dl_A2, dl_A5, dl_A6;
};

// AmigaDOS packet — the wire format for handler requests. dp_Link +
// dp_Port live in the StandardPacket's Message; dp_Type is an ACTION_*.
// CaraOS widens the result/argument slots from the classic 32-bit LONG
// to SIPTR (pointer-width): unlike 68k AmigaDOS, dp_Res1 and the args
// must carry real 64-bit pointers (a BPTR lock, a name pointer). dp_Type
// stays LONG (an ACTION_* code). V36 source assigning small LONGs to
// these still compiles.
struct DosPacket {
    struct Message *dp_Link; // the Message this packet is bound to
    struct MsgPort *dp_Port; // reply port
    LONG dp_Type;            // ACTION_* request code
    SIPTR dp_Res1;           // primary result (BOOL / BPTR / count)
    SIPTR dp_Res2;           // secondary result (an IoErr value)
    SIPTR dp_Arg1;
    SIPTR dp_Arg2;
    SIPTR dp_Arg3;
    SIPTR dp_Arg4;
    SIPTR dp_Arg5;
    SIPTR dp_Arg6;
    SIPTR dp_Arg7;
};

// A Message with its DosPacket inline — what handlers actually receive.
struct StandardPacket {
    struct Message sp_Msg;
    struct DosPacket sp_Pkt;
};

// FileLock — a BPTR to one of these is what Lock() returns. fl_Key is
// the CaraFS cnode of the locked object (docs/LOGAIC_DOS.md §4).
struct FileLock {
    BPTR fl_Link;            // next lock (BCPL list)
    BPTR fl_Key;             // CaraFS cnode of the locked object
    LONG fl_Access;          // SHARED_LOCK / EXCLUSIVE_LOCK
    struct MsgPort *fl_Task; // handler port
    BPTR fl_Volume;          // volume node
};

// FileHandle — a BPTR to one of these is what Open() returns.
struct FileHandle {
    struct Message *fh_Link;
    struct MsgPort *fh_Port;
    struct MsgPort *fh_Type; // handler port
    LONG fh_Buf;
    LONG fh_Pos; // current file offset
    LONG fh_End;
    LONG fh_Funcs;
    LONG fh_Func2;
    LONG fh_Func3;
    LONG fh_Args; // CaraFS cnode of the open file
    LONG fh_Arg2;
};

// Info() result block — volume statistics (3rd Edition dos/dosextens.h).
struct InfoData {
    LONG id_NumSoftErrors;
    LONG id_UnitNumber;
    LONG id_DiskState;
    LONG id_NumBlocks;
    LONG id_NumBlocksUsed;
    LONG id_BytesPerBlock;
    LONG id_DiskType;
    BPTR id_VolumeNode;
    LONG id_InUse;
};

#define ID_VALIDATING 81
#define ID_VALIDATED 82
#define ID_DOS_DISK 0x444F5300 // 'DOS\0'

// Packet ACTION_* request codes (the subset L3 implements; more land
// with later slices). Values are the canonical AmigaDOS codes.
#define ACTION_NIL 0
#define ACTION_LOCATE_OBJECT 8
#define ACTION_FREE_LOCK 15
#define ACTION_DELETE_OBJECT 16
#define ACTION_RENAME_OBJECT 17
#define ACTION_CREATE_DIR 22
#define ACTION_EXAMINE_OBJECT 23
#define ACTION_EXAMINE_NEXT 24
#define ACTION_DISK_INFO 25
#define ACTION_PARENT 29
#define ACTION_SET_PROTECT 21
#define ACTION_SET_COMMENT 28
#define ACTION_FINDUPDATE 1004
#define ACTION_FINDINPUT 1005
#define ACTION_FINDOUTPUT 1006
#define ACTION_READ 82  // 'R'
#define ACTION_WRITE 87 // 'W'
#define ACTION_SEEK 1008
#define ACTION_END 1007

#endif // DOS_DOSEXTENS_H
