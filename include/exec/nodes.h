// SPDX-License-Identifier: BSD-2-Clause
//
// AmigaOS V36+ exec list node primitives. Two flavours:
//
//   struct Node     — full node with name/priority/type. Used wherever
//                     a list is human-walkable or kernel-typed
//                     (resident library list, memory list, MsgPort
//                     list, FindTask name lookup).
//   struct MinNode  — pointer-only minimal node. Used where listing is
//                     purely a queue and no name/type matters
//                     (per-task signal sleeper queues, MsgPort
//                     ring-of-slots, etc.).
//
// Layouts are V36+ field-for-field (ln_*/mln_*) so V36+ source that
// walks ln_Succ / mln_Succ field-by-field reads correct addresses.
// On RV64 the on-disk *size* of a Node differs from 68k (8-byte
// pointers, natural alignment), but no V36+ source written for the
// 3rd Edition RKMs depends on the literal byte size.
//
// The kernel-internal copies of these primitives live in the
// brand-namespace <cara/list.h>, which after Phase A drift cleanup
// shares the public layouts via the same field names and helper
// signatures.

#ifndef EXEC_NODES_H
#define EXEC_NODES_H

#include <exec/types.h>

struct Node {
    struct Node *ln_Succ;
    struct Node *ln_Pred;
    UBYTE ln_Type;
    BYTE ln_Pri;
    char *ln_Name;
};

struct MinNode {
    struct MinNode *mln_Succ;
    struct MinNode *mln_Pred;
};

// ln_Type values — V36+ canonical (exec/nodes.i).
#define NT_UNKNOWN 0
#define NT_TASK 1
#define NT_INTERRUPT 2
#define NT_DEVICE 3
#define NT_MSGPORT 4
#define NT_MESSAGE 5
#define NT_FREEMSG 6
#define NT_REPLYMSG 7
#define NT_RESOURCE 8
#define NT_LIBRARY 9
#define NT_MEMORY 10
#define NT_SOFTINT 11
#define NT_FONT 12
#define NT_PROCESS 13
#define NT_SEMAPHORE 14
#define NT_SIGNALSEM 15
#define NT_BOOTNODE 16
#define NT_KICKMEM 17
#define NT_GRAPHICS 18
#define NT_DEATHMESSAGE 19
#define NT_USER 254
#define NT_EXTENDED 255

#endif // EXEC_NODES_H
