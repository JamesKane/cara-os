// SPDX-License-Identifier: BSD-2-Clause
//
// workbench/workbench.h — the V36+ Workbench AppShell / icon ABI. This
// is the API namespace: verbatim AmigaOS names, field order, and
// constants so a V36 source program compiles unchanged (docs/PRINCIPLES
// §3.1). icon.library (L11) parses an object's `cara.icon` xattr into a
// struct DiskObject; CaraOS keeps no sibling `.info` files (CARAFS
// §3.10). Pointer fields are 64-bit on CaraOS — the struct is the
// 64-bit analogue of the classic layout, not a byte-image of it (the
// on-disk form is the icon.library serialisation, not a struct dump).

#ifndef CARA_WORKBENCH_WORKBENCH_H
#define CARA_WORKBENCH_WORKBENCH_H

#include <exec/lists.h>
#include <exec/types.h>
#include <intuition/intuition.h>

// do_Type — what kind of object an icon represents.
#define WBDISK 1
#define WBDRAWER 2
#define WBTOOL 3
#define WBPROJECT 4
#define WBGARBAGE 5
#define WBDEVICE 6
#define WBKICK 7
#define WBAPPICON 8

#define WB_DISKMAGIC 0xE310
#define WB_DISKVERSION 1
#define WB_DISKREVISION 1

// do_CurrentX / do_CurrentY sentinel: "no recorded Workbench position".
#define NO_ICON_POSITION (0x80000000)

// A drawer's window geometry + scroll offset; do_DrawerData points here
// for WBDISK / WBDRAWER / WBGARBAGE icons.
struct DrawerData {
    struct NewWindow dd_NewWindow; // drawer window position/size
    LONG dd_CurrentX;              // current scroll offset
    LONG dd_CurrentY;
    ULONG dd_Flags;     // V37+ view flags
    UWORD dd_ViewModes; // V37+ show/view mode
};

#define OLDDRAWERDATAFILESIZE (sizeof(struct NewWindow) + (2 * sizeof(LONG)))
#define DRAWERDATAFILESIZE (OLDDRAWERDATAFILESIZE + sizeof(ULONG) + sizeof(UWORD))

// The icon itself, as returned by GetDiskObject.
struct DiskObject {
    UWORD do_Magic;   // WB_DISKMAGIC
    UWORD do_Version; // WB_DISKVERSION
    struct Gadget do_Gadget;
    UBYTE do_Type; // WBDISK..WBAPPICON
    char *do_DefaultTool;
    char **do_ToolTypes; // NULL-terminated array of "KEY=VALUE" strings
    LONG do_CurrentX;
    LONG do_CurrentY;
    struct DrawerData *do_DrawerData;
    char *do_ToolWindow; // WBTOOL: a tool's preferred window
    LONG do_StackSize;   // WBTOOL: stack to launch with
};

// icon.library allocation bookkeeping. CaraOS returns each DiskObject as
// one shared-heap block freed by FreeDiskObject, so this is ABI-only.
struct FreeList {
    WORD fl_NumFree;
    struct List fl_MemList;
};

#endif // CARA_WORKBENCH_WORKBENCH_H
