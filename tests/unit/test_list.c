// SPDX-License-Identifier: BSD-2-Clause
//
// Hosted unit test for cara/list.h. Exercises empty/non-empty state, head/tail
// inserts, removal of head/middle/tail, and ListNodeOf container recovery.

#include <cara/list.h>
#include <stdio.h>

struct Item {
    int v;
    struct ListNode node;
};

static int fail(const char *msg, int code)
{
    fprintf(stderr, "test_list: FAIL: %s\n", msg);
    return code;
}

static int count(const struct MinList *l)
{
    int n = 0;
    for (const struct ListNode *p = l->head.succ; p != &l->tail; p = p->succ) {
        n++;
    }
    return n;
}

static int rcount(const struct MinList *l)
{
    int n = 0;
    for (const struct ListNode *p = l->tail.pred; p != &l->head; p = p->pred) {
        n++;
    }
    return n;
}

int main(void)
{
    struct MinList l;
    ListInit(&l);

    if (!ListIsEmpty(&l)) {
        return fail("empty list reports non-empty", 1);
    }
    if (count(&l) != 0 || rcount(&l) != 0) {
        return fail("empty list has nonzero forward/reverse count", 2);
    }
    if (ListRemHead(&l) != nullptr || ListRemTail(&l) != nullptr) {
        return fail("rem on empty did not return null", 3);
    }

    struct Item a = { .v = 1 };
    struct Item b = { .v = 2 };
    struct Item c = { .v = 3 };

    // Tail-insert 1, 2, 3 in order.
    ListAddTail(&l, &a.node);
    ListAddTail(&l, &b.node);
    ListAddTail(&l, &c.node);

    if (ListIsEmpty(&l)) {
        return fail("after 3 inserts list reports empty", 4);
    }
    if (count(&l) != 3 || rcount(&l) != 3) {
        return fail("count mismatch after tail inserts", 5);
    }

    // Verify forward order is 1,2,3.
    int expected[] = { 1, 2, 3 };
    int idx = 0;
    ListForEach(p, &l) {
        struct Item *it = ListNodeOf(p, struct Item, node);
        if (it->v != expected[idx]) {
            return fail("forward order wrong", 6);
        }
        idx++;
    }
    if (idx != 3) {
        return fail("ListForEach yielded wrong element count", 7);
    }

    // Verify reverse order is 3,2,1.
    int rexp[] = { 3, 2, 1 };
    idx = 0;
    for (struct ListNode *p = l.tail.pred; p != &l.head; p = p->pred) {
        struct Item *it = ListNodeOf(p, struct Item, node);
        if (it->v != rexp[idx]) {
            return fail("reverse order wrong", 8);
        }
        idx++;
    }

    // Remove the middle.
    ListRemove(&b.node);
    if (b.node.succ != nullptr || b.node.pred != nullptr) {
        return fail("removed node not nulled", 9);
    }
    if (count(&l) != 2 || rcount(&l) != 2) {
        return fail("count wrong after middle remove", 10);
    }

    // Remove head via ListRemHead — should return &a.node.
    struct ListNode *got = ListRemHead(&l);
    if (got != &a.node) {
        return fail("ListRemHead did not return first element", 11);
    }
    struct Item *got_item = ListNodeOf(got, struct Item, node);
    if (got_item->v != 1) {
        return fail("ListNodeOf recovered wrong container", 12);
    }

    // Remove tail via ListRemTail — should return &c.node.
    got = ListRemTail(&l);
    if (got != &c.node || ListNodeOf(got, struct Item, node)->v != 3) {
        return fail("ListRemTail returned wrong element", 13);
    }

    if (!ListIsEmpty(&l)) {
        return fail("list not empty after all elements removed", 14);
    }

    // Head-insert ordering: insert 1, 2, 3 at head; expect forward order 3,2,1.
    ListInit(&l);
    ListAddHead(&l, &a.node);
    ListAddHead(&l, &b.node);
    ListAddHead(&l, &c.node);
    int hexp[] = { 3, 2, 1 };
    idx = 0;
    ListForEach(p, &l) {
        if (ListNodeOf(p, struct Item, node)->v != hexp[idx++]) {
            return fail("head-insert ordering wrong", 15);
        }
    }

    puts("list smoke ok");
    return 0;
}
