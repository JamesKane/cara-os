// SPDX-License-Identifier: BSD-2-Clause
//
// Hosted unit test for cara/list.h. Exercises empty/non-empty state, head/tail
// inserts, removal of head/middle/tail, and MinList_NodeOf container recovery.
// The list helpers and struct layouts are V36+ canonical (mlh_* / mln_*).

#include <cara/list.h>
#include <stdio.h>

struct Item {
    int v;
    struct MinNode node;
};

static int fail(const char *msg, int code)
{
    fprintf(stderr, "test_list: FAIL: %s\n", msg);
    return code;
}

static int count(const struct MinList *l)
{
    int n = 0;
    for (const struct MinNode *p = l->mlh_Head; p->mln_Succ; p = p->mln_Succ) {
        n++;
    }
    return n;
}

static int rcount(const struct MinList *l)
{
    int n = 0;
    for (const struct MinNode *p = l->mlh_TailPred; p->mln_Pred; p = p->mln_Pred) {
        n++;
    }
    return n;
}

int main(void)
{
    struct MinList l;
    MinList_Init(&l);

    if (!MinList_IsEmpty(&l)) {
        return fail("empty list reports non-empty", 1);
    }
    if (count(&l) != 0 || rcount(&l) != 0) {
        return fail("empty list has nonzero forward/reverse count", 2);
    }
    if (MinList_RemHead(&l) != nullptr || MinList_RemTail(&l) != nullptr) {
        return fail("rem on empty did not return null", 3);
    }

    struct Item a = { .v = 1 };
    struct Item b = { .v = 2 };
    struct Item c = { .v = 3 };

    // Tail-insert 1, 2, 3 in order.
    MinList_AddTail(&l, &a.node);
    MinList_AddTail(&l, &b.node);
    MinList_AddTail(&l, &c.node);

    if (MinList_IsEmpty(&l)) {
        return fail("after 3 inserts list reports empty", 4);
    }
    if (count(&l) != 3 || rcount(&l) != 3) {
        return fail("count mismatch after tail inserts", 5);
    }

    // Verify forward order is 1,2,3.
    int expected[] = { 1, 2, 3 };
    int idx = 0;
    MinList_ForEach(p, &l) {
        struct Item *it = MinList_NodeOf(p, struct Item, node);
        if (it->v != expected[idx]) {
            return fail("forward order wrong", 6);
        }
        idx++;
    }
    if (idx != 3) {
        return fail("MinList_ForEach yielded wrong element count", 7);
    }

    // Verify reverse order is 3,2,1.
    int rexp[] = { 3, 2, 1 };
    idx = 0;
    for (struct MinNode *p = l.mlh_TailPred; p->mln_Pred; p = p->mln_Pred) {
        struct Item *it = MinList_NodeOf(p, struct Item, node);
        if (it->v != rexp[idx]) {
            return fail("reverse order wrong", 8);
        }
        idx++;
    }

    // Remove the middle.
    MinList_Remove(&b.node);
    if (b.node.mln_Succ != nullptr || b.node.mln_Pred != nullptr) {
        return fail("removed node not nulled", 9);
    }
    if (count(&l) != 2 || rcount(&l) != 2) {
        return fail("count wrong after middle remove", 10);
    }

    // Remove head via MinList_RemHead — should return &a.node.
    struct MinNode *got = MinList_RemHead(&l);
    if (got != &a.node) {
        return fail("MinList_RemHead did not return first element", 11);
    }
    struct Item *got_item = MinList_NodeOf(got, struct Item, node);
    if (got_item->v != 1) {
        return fail("MinList_NodeOf recovered wrong container", 12);
    }

    // Remove tail via MinList_RemTail — should return &c.node.
    got = MinList_RemTail(&l);
    if (got != &c.node || MinList_NodeOf(got, struct Item, node)->v != 3) {
        return fail("MinList_RemTail returned wrong element", 13);
    }

    if (!MinList_IsEmpty(&l)) {
        return fail("list not empty after all elements removed", 14);
    }

    // Head-insert ordering: insert 1, 2, 3 at head; expect forward order 3,2,1.
    MinList_Init(&l);
    MinList_AddHead(&l, &a.node);
    MinList_AddHead(&l, &b.node);
    MinList_AddHead(&l, &c.node);
    int hexp[] = { 3, 2, 1 };
    idx = 0;
    MinList_ForEach(p, &l) {
        if (MinList_NodeOf(p, struct Item, node)->v != hexp[idx++]) {
            return fail("head-insert ordering wrong", 15);
        }
    }

    puts("list smoke ok");
    return 0;
}
