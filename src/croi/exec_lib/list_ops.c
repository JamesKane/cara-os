// SPDX-License-Identifier: BSD-2-Clause
//
// exec.library list primitives (Phase 3 L1; LVO.md §5.1). These are the
// first U-mode-callable `local`-flavour LVOs: the vec table points
// straight at these C functions, the <proto/exec.h> inline stub jalrs to
// them in-process, no syscall. They run from the shared library RX page
// (section .lib_text.exec, mapped at user VA 0x4000_0000), so they must
// be fully self-contained — only stack + their argument pointers, no
// kernel symbols, no globals, no out-of-section calls.
//
// They operate on the caller's struct List / struct Node, which under
// SASOS are valid pointers in both the calling task and here. Semantics
// are the canonical AmigaOS ones, including the header-overlay trick: a
// struct List's first two fields (lh_Head, lh_Tail) overlay a struct
// Node's (ln_Succ, ln_Pred), so &lh_Head and &lh_Tail double as the
// head/tail sentinel nodes — that is what lets Remove() update the list
// head/tail uniformly. The void* casts keep -fstrict-aliasing quiet.
//
// The generated stub appends SysBase as the trailing argument; the impls
// take it as `struct ExecBase *` and ignore it.

#include <exec/lists.h>
#include <exec/nodes.h>
#include <exec/types.h>

struct ExecBase; // opaque here — the appended base arg is unused

#define LIBTEXT __attribute__((section(".lib_text.exec"), used))

// The header/tail sentinels, expressed inline at each use site (a helper
// function would be a call out of .lib_text.exec — out of PC-relative
// range from the RX page). HEAD_NODE: &lh_Head viewed as a Node
// (ln_Succ=lh_Head, ln_Pred=lh_Tail). TAIL_NODE: &lh_Tail viewed as a
// Node (ln_Succ=lh_Tail==NULL, ln_Pred=lh_TailPred).
#define HEAD_NODE(l) ((struct Node *)(void *)&(l)->lh_Head)
#define TAIL_NODE(l) ((struct Node *)(void *)&(l)->lh_Tail)

LIBTEXT void Croi_Exec_AddHead(struct List *list, struct Node *node, struct ExecBase *base)
{
    (void)base;
    struct Node *head = list->lh_Head;
    node->ln_Succ = head;
    node->ln_Pred = HEAD_NODE(list);
    head->ln_Pred = node;
    list->lh_Head = node;
}

LIBTEXT void Croi_Exec_AddTail(struct List *list, struct Node *node, struct ExecBase *base)
{
    (void)base;
    struct Node *tailpred = list->lh_TailPred;
    node->ln_Succ = TAIL_NODE(list);
    node->ln_Pred = tailpred;
    tailpred->ln_Succ = node;
    list->lh_TailPred = node;
}

LIBTEXT void Croi_Exec_Remove(struct Node *node, struct ExecBase *base)
{
    (void)base;
    node->ln_Pred->ln_Succ = node->ln_Succ;
    node->ln_Succ->ln_Pred = node->ln_Pred;
}

LIBTEXT struct Node *Croi_Exec_RemHead(struct List *list, struct ExecBase *base)
{
    (void)base;
    struct Node *node = list->lh_Head;
    struct Node *succ = node->ln_Succ;
    if (succ == nullptr) {
        return nullptr; // empty: lh_Head is the tail sentinel
    }
    list->lh_Head = succ;
    succ->ln_Pred = HEAD_NODE(list);
    return node;
}

LIBTEXT struct Node *Croi_Exec_RemTail(struct List *list, struct ExecBase *base)
{
    (void)base;
    struct Node *node = list->lh_TailPred;
    struct Node *pred = node->ln_Pred;
    if (pred == nullptr) {
        return nullptr; // empty: lh_TailPred is the head sentinel
    }
    list->lh_TailPred = pred;
    pred->ln_Succ = TAIL_NODE(list);
    return node;
}

LIBTEXT void Croi_Exec_Insert(struct List *list, struct Node *node, struct Node *pred,
                              struct ExecBase *base)
{
    (void)base;
    // Insert after `pred`. If pred is the last real node, its successor
    // is the tail sentinel and the node lands at the tail — correct.
    if (pred != nullptr && pred->ln_Succ != nullptr) {
        struct Node *succ = pred->ln_Succ;
        node->ln_Pred = pred;
        node->ln_Succ = succ;
        succ->ln_Pred = node;
        pred->ln_Succ = node;
        return;
    }
    // pred NULL (or the tail sentinel) → add at the head.
    struct Node *head = list->lh_Head;
    node->ln_Succ = head;
    node->ln_Pred = HEAD_NODE(list);
    head->ln_Pred = node;
    list->lh_Head = node;
}

LIBTEXT void Croi_Exec_Enqueue(struct List *list, struct Node *node, struct ExecBase *base)
{
    (void)base;
    // Insert keeping the list sorted by ln_Pri descending; the node goes
    // after all nodes of >= priority. cur walks the header then real
    // nodes; a real node has a non-NULL ln_Succ (the tail sentinel's is
    // NULL), so (next->ln_Succ != NULL) means "next is a real node".
    struct Node *cur = HEAD_NODE(list);
    struct Node *next;
    while ((next = cur->ln_Succ)->ln_Succ != nullptr) {
        if (next->ln_Pri < node->ln_Pri) {
            break;
        }
        cur = next;
    }
    node->ln_Pred = cur;
    node->ln_Succ = next;
    next->ln_Pred = node;
    cur->ln_Succ = node;
}

LIBTEXT struct Node *Croi_Exec_FindName(struct List *list, STRPTR name, struct ExecBase *base)
{
    (void)base;
    const unsigned char *want = (const unsigned char *)name;
    for (struct Node *n = list->lh_Head; n->ln_Succ != nullptr; n = n->ln_Succ) {
        const unsigned char *have = (const unsigned char *)n->ln_Name;
        if (have == nullptr) {
            continue;
        }
        int i = 0;
        while (have[i] != 0 && have[i] == want[i]) {
            i++;
        }
        if (have[i] == 0 && want[i] == 0) {
            return n;
        }
    }
    return nullptr;
}
