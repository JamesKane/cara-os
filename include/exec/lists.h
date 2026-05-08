// SPDX-License-Identifier: BSD-2-Clause
//
// AmigaOS V36+ exec list headers. Both flavours are three-pointer
// (head / tail / tailpred) sentinel headers — the lh_Tail field is
// always NULL and lh_TailPred lets `Insert at tail` be done without a
// special case for the empty list. This is the V36+ canonical idiom.
//
//   struct List     — pairs with struct Node    (named/typed/priority).
//   struct MinList  — pairs with struct MinNode (pointer-only).
//
// The classic empty-list test is:
//   IsListEmpty(lh) ≡ lh.lh_TailPred == (struct Node *)&lh
// because in an empty list lh_TailPred points back at the header
// itself (interpreted as a Node), and lh_Head also points at &lh_Tail.
//
// LVO trampolines for AddHead / AddTail / RemHead / RemTail / Remove
// / Enqueue / FindName / NewList live in exec.library (Phase 3 wiring,
// after Phase A drift cleanup lands the kernel-internal MinList_*
// helpers under the same layout).

#ifndef EXEC_LISTS_H
#define EXEC_LISTS_H

#include <exec/nodes.h>
#include <exec/types.h>

struct List {
    struct Node *lh_Head;
    struct Node *lh_Tail;       // always NULL — sentinel
    struct Node *lh_TailPred;
    UBYTE        lh_Type;
    UBYTE        l_pad;
};

struct MinList {
    struct MinNode *mlh_Head;
    struct MinNode *mlh_Tail;       // always NULL
    struct MinNode *mlh_TailPred;
};

// V36+ exec/lists.h IsListEmpty macro. Works on both struct List and
// struct MinList because the first three fields are pointer-compatible
// and the test only touches the third.
#define IsListEmpty(lh) \
    (((struct List *)(lh))->lh_TailPred == (struct Node *)(lh))

#endif // EXEC_LISTS_H
