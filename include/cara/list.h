// SPDX-License-Identifier: BSD-2-Clause
//
// Cara intrusive doubly-linked list helpers.
//
// The struct layouts (struct MinList, struct MinNode) come from the
// AmigaOS V36+ public header <exec/lists.h>. This header just adds
// brand-namespace MinList_* helpers that kernel code uses for
// internal queues — they perform exactly the AmigaOS V36+ exec.library
// AddHead/AddTail/RemHead/RemTail/Remove memory operations. Phase C
// will publish those LVO names from exec.library on top of these
// helpers; the inlines here are what the LVO bodies will call.
//
// V36+ tailpred-sentinel idiom:
//
//     struct MinList l; MinList_Init(&l);
//     struct Frob f = { .v = 7 };
//     MinList_AddTail(&l, &f.node);
//     for (struct MinNode *n = l.mlh_Head; n->mln_Succ; n = n->mln_Succ) {
//         struct Frob *p = MinList_NodeOf(n, struct Frob, node);
//         // ...
//     }

#ifndef CARA_LIST_H
#define CARA_LIST_H

#include <cara/types.h>
#include <exec/lists.h>

static inline void MinList_Init(struct MinList *l)
{
    l->mlh_Head = (struct MinNode *)&l->mlh_Tail;
    l->mlh_Tail = nullptr;
    l->mlh_TailPred = (struct MinNode *)&l->mlh_Head;
}

[[nodiscard]] static inline bool MinList_IsEmpty(const struct MinList *l)
{
    return l->mlh_TailPred == (const struct MinNode *)&l->mlh_Head;
}

static inline void MinList_AddHead(struct MinList *l, struct MinNode *n)
{
    n->mln_Pred = (struct MinNode *)&l->mlh_Head;
    n->mln_Succ = l->mlh_Head;
    l->mlh_Head->mln_Pred = n;
    l->mlh_Head = n;
}

static inline void MinList_AddTail(struct MinList *l, struct MinNode *n)
{
    n->mln_Succ = (struct MinNode *)&l->mlh_Tail;
    n->mln_Pred = l->mlh_TailPred;
    l->mlh_TailPred->mln_Succ = n;
    l->mlh_TailPred = n;
}

static inline void MinList_Remove(struct MinNode *n)
{
    n->mln_Pred->mln_Succ = n->mln_Succ;
    n->mln_Succ->mln_Pred = n->mln_Pred;
    n->mln_Succ = nullptr;
    n->mln_Pred = nullptr;
}

static inline struct MinNode *MinList_RemHead(struct MinList *l)
{
    if (MinList_IsEmpty(l)) {
        return nullptr;
    }
    struct MinNode *n = l->mlh_Head;
    MinList_Remove(n);
    return n;
}

static inline struct MinNode *MinList_RemTail(struct MinList *l)
{
    if (MinList_IsEmpty(l)) {
        return nullptr;
    }
    struct MinNode *n = l->mlh_TailPred;
    MinList_Remove(n);
    return n;
}

#define MinList_NodeOf(PTR, TYPE, MEMBER) ((TYPE *)(void *)((char *)(PTR) - offsetof(TYPE, MEMBER)))

// Forward iteration. The terminating condition `(VAR)->mln_Succ` is true
// for every real node and false at the virtual tail-node (whose
// mln_Succ aliases mlh_Tail = nullptr).
#define MinList_ForEach(VAR, LIST)                                                                 \
    for (struct MinNode *VAR = (LIST)->mlh_Head; (VAR)->mln_Succ; (VAR) = (VAR)->mln_Succ)

#endif
