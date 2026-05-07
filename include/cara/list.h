// SPDX-License-Identifier: BSD-2-Clause
//
// Cara intrusive doubly-linked list. The structure follows AmigaOS Exec's
// MinList: a header with two sentinel nodes (head, tail). The first real
// element is always head.succ; the last is always tail.pred. An empty list
// has head.succ == &tail. The sentinels keep insert/remove branchless.
//
// Callers embed a struct ListNode in their type:
//
//     struct Frob { int v; struct ListNode node; };
//     struct MinList l; ListInit(&l);
//     struct Frob f = { .v = 7 };
//     ListAddTail(&l, &f.node);
//     for (struct ListNode *n = l.head.succ; n != &l.tail; n = n->succ) {
//         struct Frob *p = ListNodeOf(n, struct Frob, node);
//         // ...
//     }

#ifndef CARA_LIST_H
#define CARA_LIST_H

#include <cara/types.h>

struct ListNode {
    struct ListNode *succ;
    struct ListNode *pred;
};

struct MinList {
    struct ListNode head;
    struct ListNode tail;
};

static inline void ListInit(struct MinList *l)
{
    l->head.succ = &l->tail;
    l->head.pred = nullptr;
    l->tail.succ = nullptr;
    l->tail.pred = &l->head;
}

[[nodiscard]] static inline bool ListIsEmpty(const struct MinList *l)
{
    return l->head.succ == &l->tail;
}

static inline void ListAddHead(struct MinList *l, struct ListNode *n)
{
    n->pred = &l->head;
    n->succ = l->head.succ;
    l->head.succ->pred = n;
    l->head.succ = n;
}

static inline void ListAddTail(struct MinList *l, struct ListNode *n)
{
    n->succ = &l->tail;
    n->pred = l->tail.pred;
    l->tail.pred->succ = n;
    l->tail.pred = n;
}

static inline void ListRemove(struct ListNode *n)
{
    n->pred->succ = n->succ;
    n->succ->pred = n->pred;
    n->succ = nullptr;
    n->pred = nullptr;
}

static inline struct ListNode *ListRemHead(struct MinList *l)
{
    if (ListIsEmpty(l)) {
        return nullptr;
    }
    struct ListNode *n = l->head.succ;
    ListRemove(n);
    return n;
}

static inline struct ListNode *ListRemTail(struct MinList *l)
{
    if (ListIsEmpty(l)) {
        return nullptr;
    }
    struct ListNode *n = l->tail.pred;
    ListRemove(n);
    return n;
}

#define ListNodeOf(PTR, TYPE, MEMBER) \
    ((TYPE *)(void *)((char *)(PTR) - offsetof(TYPE, MEMBER)))

#define ListForEach(VAR, LIST) \
    for (struct ListNode *VAR = (LIST)->head.succ; \
         VAR != &(LIST)->tail; \
         VAR = VAR->succ)

#endif
