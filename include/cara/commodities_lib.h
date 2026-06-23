// SPDX-License-Identifier: BSD-2-Clause
//
// Kernel-side impl prototypes for the V36+ commodities.library LVOs Croi
// serves (docs/COMMODITIES.md). commodities is `syscall` flavour: each LVO
// reaches src/croi/syscall/syscall.c via its trampoline (Cara_Trampoline_
// Cx_* in src/croi/commodities/trampolines.S), routed to the Croi_Cx_*_Impl
// below. L13.1 is the CxObj object model — a broker/object tree built,
// queried, and torn down in the SASOS shared heap; the live input dispatch
// (the input.device handler chain) is deferred. ParseIX + the CxMsg
// accessors arrive in L13.2.

#ifndef CARA_COMMODITIES_LIB_H
#define CARA_COMMODITIES_LIB_H

#include <cara/types.h>
#include <exec/types.h>
#include <libraries/commodities.h>

struct Library;

// Kernel-private CxObj. The opaque public CxObj * IS this pointer (CxObj
// has no public fields); the impls cast back. A broker is the tree root;
// AttachCxObj/EnqueueCxObj/InsertCxObj link children into `children`
// (singly-linked via `next`), DeleteCxObjAll frees the whole subtree.
struct CaraCxObj {
    ULONG type; // CX_*
    LONG pri;
    LONG error; // COERR_* bits
    bool active;
    struct CaraCxObj *parent;
    struct CaraCxObj *next;     // next sibling in parent's child list
    struct CaraCxObj *children; // head of child list
    IPTR arg1;                  // CreateCxObj type-specific args (port/id/desc/…)
    IPTR arg2;
    struct InputXpression ix; // CX_FILTER parsed expression (L13.2)
    bool has_ix;
};

// ---- Reserved-slot library hooks (`local` flavour) ------------------
struct Library *Croi_Cx_Open(struct Library *base, struct CxBase *cb);
void Croi_Cx_Close(struct Library *base, struct CxBase *cb);
void Croi_Cx_Expunge(struct Library *base, struct CxBase *cb);
ULONG Croi_Cx_ExtFunc(struct Library *base, struct CxBase *cb);

// ---- Object model (L13.1) -------------------------------------------
struct CxObj *Croi_Cx_CreateCxObj_Impl(ULONG type, IPTR arg1, IPTR arg2);
struct CxObj *Croi_Cx_CxBroker_Impl(struct NewBroker *nb, LONG *error);
LONG Croi_Cx_ActivateCxObj_Impl(struct CxObj *co, LONG doactivate);
void Croi_Cx_DeleteCxObj_Impl(struct CxObj *co);
void Croi_Cx_DeleteCxObjAll_Impl(struct CxObj *co);
ULONG Croi_Cx_CxObjType_Impl(struct CxObj *co);
LONG Croi_Cx_CxObjError_Impl(struct CxObj *co);
void Croi_Cx_ClearCxObjError_Impl(struct CxObj *co);
LONG Croi_Cx_SetCxObjPri_Impl(struct CxObj *co, LONG pri);
void Croi_Cx_AttachCxObj_Impl(struct CxObj *headObj, struct CxObj *co);
void Croi_Cx_EnqueueCxObj_Impl(struct CxObj *headObj, struct CxObj *co);
void Croi_Cx_InsertCxObj_Impl(struct CxObj *headObj, struct CxObj *co, struct CxObj *pred);
void Croi_Cx_RemoveCxObj_Impl(struct CxObj *co);

#endif // CARA_COMMODITIES_LIB_H
