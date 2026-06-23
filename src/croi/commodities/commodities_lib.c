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

// ---- Input config + ParseIX + CxMsg accessors (L13.2) ---------------

void Croi_Cx_SetTranslate_Impl(struct CxObj *translator, struct InputEvent *events)
{
    if (translator) {
        // Store the translation list; emitted live once dispatch is wired.
        ((struct CaraCxObj *)translator)->arg1 = (IPTR)(uptr)events;
    }
}

LONG Croi_Cx_SetFilterIX_Impl(struct CxObj *filter, struct InputXpression *ix)
{
    if (!filter || !ix) {
        return COERR_BADFILTER;
    }
    struct CaraCxObj *o = (struct CaraCxObj *)filter;
    o->ix = *ix;
    o->has_ix = true;
    return 0;
}

void Croi_Cx_SetFilter_Impl(struct CxObj *filter, STRPTR text)
{
    if (!filter) {
        return;
    }
    struct CaraCxObj *o = (struct CaraCxObj *)filter;
    struct InputXpression ix;
    if (Croi_Cx_ParseIX_Impl(text, &ix) == IXERR_OK) {
        o->ix = ix;
        o->has_ix = true;
    } else {
        o->error |= COERR_BADFILTER;
    }
}

// ---- ParseIX ---------------------------------------------------------

static bool cx_ci_eq(const char *tok, u32 len, const char *name)
{
    for (u32 i = 0; i < len; i++) {
        char a = tok[i];
        char b = name[i];
        if (!b) {
            return false; // name shorter than token
        }
        if (a >= 'A' && a <= 'Z') {
            a = (char)(a - 'A' + 'a');
        }
        if (a != b) {
            return false;
        }
    }
    return name[len] == 0; // both exactly len
}

struct cx_kv {
    const char *name;
    UWORD value;
};

// Qualifier words → IEQUALIFIER_* bit (synonyms map to a single side in v0).
static const struct cx_kv cx_quals[] = {
    { "shift", IEQUALIFIER_LSHIFT },
    { "lshift", IEQUALIFIER_LSHIFT },
    { "rshift", IEQUALIFIER_RSHIFT },
    { "capslock", IEQUALIFIER_CAPSLOCK },
    { "caps", IEQUALIFIER_CAPSLOCK },
    { "control", IEQUALIFIER_CONTROL },
    { "ctrl", IEQUALIFIER_CONTROL },
    { "alt", IEQUALIFIER_LALT },
    { "lalt", IEQUALIFIER_LALT },
    { "ralt", IEQUALIFIER_RALT },
    { "lcommand", IEQUALIFIER_LCOMMAND },
    { "lamiga", IEQUALIFIER_LCOMMAND },
    { "rcommand", IEQUALIFIER_RCOMMAND },
    { "ramiga", IEQUALIFIER_RCOMMAND },
    { "numericpad", IEQUALIFIER_NUMERICPAD },
    { "numpad", IEQUALIFIER_NUMERICPAD },
    { "repeat", IEQUALIFIER_REPEAT },
    { "midbutton", IEQUALIFIER_MIDBUTTON },
    { "rbutton", IEQUALIFIER_RBUTTON },
    { "leftbutton", IEQUALIFIER_LEFTBUTTON },
    { nullptr, 0 },
};

// Named keys → Amiga rawkey code (the common subset; ascii/keymap-derived
// keys are deferred with keymap.library).
static const struct cx_kv cx_keys[] = {
    { "f1", 0x50 },        { "f2", 0x51 },   { "f3", 0x52 },     { "f4", 0x53 },
    { "f5", 0x54 },        { "f6", 0x55 },   { "f7", 0x56 },     { "f8", 0x57 },
    { "f9", 0x58 },        { "f10", 0x59 },  { "esc", 0x45 },    { "escape", 0x45 },
    { "space", 0x40 },     { "tab", 0x42 },  { "return", 0x44 }, { "enter", 0x44 },
    { "backspace", 0x41 }, { "del", 0x46 },  { "delete", 0x46 }, { "help", 0x5F },
    { "up", 0x4C },        { "down", 0x4D }, { "right", 0x4E },  { "left", 0x4F },
    { nullptr, 0 },
};

static const struct cx_kv cx_classes[] = {
    { "rawkey", IECLASS_RAWKEY },
    { "rawmouse", IECLASS_RAWMOUSE },
    { "nullevent", IECLASS_NULL },
    { nullptr, 0 },
};

static UWORD cx_lookup(const struct cx_kv *t, const char *tok, u32 len, bool *found)
{
    for (u32 i = 0; t[i].name; i++) {
        if (cx_ci_eq(tok, len, t[i].name)) {
            *found = true;
            return t[i].value;
        }
    }
    *found = false;
    return 0;
}

LONG Croi_Cx_ParseIX_Impl(STRPTR description, struct InputXpression *ix)
{
    if (!ix) {
        return IXERR_BADSPEC;
    }
    *ix = (struct InputXpression){ 0 };
    ix->ix_Version = IX_VERSION;
    if (!description) {
        return IXERR_BADSPEC;
    }
    UWORD qual = 0, klass = 0, code = 0;
    bool have_class = false, have_code = false;
    const char *p = (const char *)description;
    while (*p) {
        while (*p == ' ' || *p == '\t') {
            p++;
        }
        if (!*p) {
            break;
        }
        const char *start = p;
        while (*p && *p != ' ' && *p != '\t') {
            p++;
        }
        u32 len = (u32)(p - start);
        bool found;
        UWORD q = cx_lookup(cx_quals, start, len, &found);
        if (found) {
            qual |= q;
            continue;
        }
        UWORD cl = cx_lookup(cx_classes, start, len, &found);
        if (found) {
            klass = cl;
            have_class = true;
            continue;
        }
        UWORD kc = cx_lookup(cx_keys, start, len, &found);
        if (found) {
            code = kc;
            have_code = true;
            continue;
        }
        return IXERR_BADSPEC; // unrecognised token (ascii keys deferred)
    }
    if (have_code) {
        ix->ix_Class = (UBYTE)(have_class ? klass : IECLASS_RAWKEY);
        ix->ix_Code = code;
        ix->ix_CodeMask = 0xFFFF;
    } else if (have_class) {
        ix->ix_Class = (UBYTE)klass;
    }
    ix->ix_Qualifier = qual;
    ix->ix_QualMask = qual; // match requires exactly these qualifiers
    ix->ix_QualSame = 0;
    return IXERR_OK;
}

// ---- CxMsg ----------------------------------------------------------

struct CxMsg *Croi_Cx_AllocCxMsg(ULONG type, IPTR data, ULONG id)
{
    struct CaraCxMsg *m = (struct CaraCxMsg *)Croi_AllocShared(sizeof *m);
    if (!m) {
        return nullptr;
    }
    m->type = type;
    m->data = data;
    m->id = id;
    return (struct CxMsg *)m;
}

ULONG Croi_Cx_CxMsgType_Impl(struct CxMsg *cxm)
{
    return cxm ? ((struct CaraCxMsg *)cxm)->type : 0u;
}

APTR Croi_Cx_CxMsgData_Impl(struct CxMsg *cxm)
{
    return cxm ? (APTR)(uptr)((struct CaraCxMsg *)cxm)->data : nullptr;
}

ULONG Croi_Cx_CxMsgID_Impl(struct CxMsg *cxm)
{
    return cxm ? ((struct CaraCxMsg *)cxm)->id : 0u;
}

void Croi_Cx_DisposeCxMsg_Impl(struct CxMsg *cxm)
{
    if (cxm) {
        Croi_Free(cxm);
    }
}
