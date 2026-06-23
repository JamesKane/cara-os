// SPDX-License-Identifier: BSD-2-Clause
//
// commodities.library CxObj object model (L13.1, docs/COMMODITIES.md). A
// commodity builds a tree of CxObjs — a broker at the root with filters /
// translators / senders attached — that the input.device handler chain
// will (once it is live, deferred) run InputEvents through. L13.1 ships the
// structural half: create/query/destroy objects and link them into the
// broker tree, all on the SASOS shared heap. No live input dispatch yet.
//
// The opaque public CxObj * IS the CaraCxObj pointer (CxObj has no public
// fields); every impl casts back. Children hang off `children` (singly
// linked via `next`); DeleteCxObj unlinks from the parent then frees the
// whole subtree.

#include <cara/alloc.h>
#include <cara/commodities_lib.h>
#include <cara/shared.h>
#include <cara/types.h>
#include <exec/types.h>
#include <libraries/commodities.h>

static struct CaraCxObj *cx_alloc(ULONG type)
{
    struct CaraCxObj *o = (struct CaraCxObj *)Croi_AllocShared(sizeof *o);
    if (!o) {
        return nullptr;
    }
    *o = (struct CaraCxObj){ 0 };
    o->type = type;
    return o;
}

struct CxObj *Croi_Cx_CreateCxObj_Impl(ULONG type, IPTR arg1, IPTR arg2)
{
    struct CaraCxObj *o = cx_alloc(type);
    if (!o) {
        return nullptr;
    }
    o->arg1 = arg1;
    o->arg2 = arg2;
    if (type == CX_BROKER && arg1) {
        const struct NewBroker *nb = (const struct NewBroker *)(uptr)arg1;
        o->pri = nb->nb_Pri;
    }
    return (struct CxObj *)o;
}

struct CxObj *Croi_Cx_CxBroker_Impl(struct NewBroker *nb, LONG *error)
{
    if (!nb) {
        if (error) {
            *error = 1; // CBERR_SYSERR
        }
        return nullptr;
    }
    if (error) {
        *error = 0; // CBERR_OK
    }
    return Croi_Cx_CreateCxObj_Impl(CX_BROKER, (IPTR)(uptr)nb, 0);
}

LONG Croi_Cx_ActivateCxObj_Impl(struct CxObj *co, LONG doactivate)
{
    if (!co) {
        return 0;
    }
    struct CaraCxObj *o = (struct CaraCxObj *)co;
    LONG old = o->active ? 1 : 0;
    o->active = doactivate != 0;
    return old;
}

ULONG Croi_Cx_CxObjType_Impl(struct CxObj *co)
{
    return co ? ((struct CaraCxObj *)co)->type : (ULONG)CX_INVALID;
}

LONG Croi_Cx_CxObjError_Impl(struct CxObj *co)
{
    return co ? ((struct CaraCxObj *)co)->error : (LONG)COERR_ISNULL;
}

void Croi_Cx_ClearCxObjError_Impl(struct CxObj *co)
{
    if (co) {
        ((struct CaraCxObj *)co)->error = 0;
    }
}

LONG Croi_Cx_SetCxObjPri_Impl(struct CxObj *co, LONG pri)
{
    if (!co) {
        return 0;
    }
    struct CaraCxObj *o = (struct CaraCxObj *)co;
    LONG old = o->pri;
    if (pri < -128) {
        pri = -128;
    }
    if (pri > 127) {
        pri = 127;
    }
    o->pri = pri;
    return old;
}

void Croi_Cx_AttachCxObj_Impl(struct CxObj *headObj, struct CxObj *co)
{
    if (!headObj || !co) {
        if (headObj) {
            ((struct CaraCxObj *)headObj)->error |= COERR_NULLATTACH;
        }
        return;
    }
    struct CaraCxObj *h = (struct CaraCxObj *)headObj;
    struct CaraCxObj *c = (struct CaraCxObj *)co;
    c->parent = h;
    c->next = nullptr;
    if (!h->children) {
        h->children = c;
        return;
    }
    struct CaraCxObj *t = h->children;
    while (t->next) {
        t = t->next;
    }
    t->next = c;
}

void Croi_Cx_EnqueueCxObj_Impl(struct CxObj *headObj, struct CxObj *co)
{
    if (!headObj || !co) {
        return;
    }
    struct CaraCxObj *h = (struct CaraCxObj *)headObj;
    struct CaraCxObj *c = (struct CaraCxObj *)co;
    c->parent = h;
    struct CaraCxObj **pp = &h->children;
    while (*pp && (*pp)->pri >= c->pri) {
        pp = &(*pp)->next;
    }
    c->next = *pp;
    *pp = c;
}

void Croi_Cx_InsertCxObj_Impl(struct CxObj *headObj, struct CxObj *co, struct CxObj *pred)
{
    if (!headObj || !co) {
        return;
    }
    struct CaraCxObj *h = (struct CaraCxObj *)headObj;
    struct CaraCxObj *c = (struct CaraCxObj *)co;
    struct CaraCxObj *p = (struct CaraCxObj *)pred;
    c->parent = h;
    if (!p) {
        c->next = h->children;
        h->children = c;
    } else {
        c->next = p->next;
        p->next = c;
    }
}

void Croi_Cx_RemoveCxObj_Impl(struct CxObj *co)
{
    if (!co) {
        return;
    }
    struct CaraCxObj *c = (struct CaraCxObj *)co;
    struct CaraCxObj *h = c->parent;
    if (h) {
        struct CaraCxObj **pp = &h->children;
        while (*pp && *pp != c) {
            pp = &(*pp)->next;
        }
        if (*pp == c) {
            *pp = c->next;
        }
    }
    c->parent = nullptr;
    c->next = nullptr;
}

// Free a CxObj and its whole subtree (depth-first).
static void cx_free_subtree(struct CaraCxObj *o)
{
    struct CaraCxObj *c = o->children;
    while (c) {
        struct CaraCxObj *n = c->next;
        cx_free_subtree(c);
        c = n;
    }
    Croi_Free(o);
}

void Croi_Cx_DeleteCxObj_Impl(struct CxObj *co)
{
    if (!co) {
        return;
    }
    Croi_Cx_RemoveCxObj_Impl(co); // unlink from any parent first
    cx_free_subtree((struct CaraCxObj *)co);
}

void Croi_Cx_DeleteCxObjAll_Impl(struct CxObj *co)
{
    // Delete the object and everything attached to it (the whole subtree).
    Croi_Cx_DeleteCxObj_Impl(co);
}
