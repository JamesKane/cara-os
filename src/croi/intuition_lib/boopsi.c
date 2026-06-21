// SPDX-License-Identifier: BSD-2-Clause
//
// BOOPSI class/object core (L7.1, docs/LEARGAS_BOOPSI.md). These are the
// `local`-flavour intuition LVOs plus the built-in rootclass dispatcher:
// all run in U-mode from the .lib_text.intuition RX page (mapped at
// 0x4000_0000), because a class's dispatcher is arbitrary U-mode code the
// S-mode kernel can never call (the L1 RX-page rule). Object and IClass
// memory comes from the SASOS shared heap via an INLINED AllocVec/FreeVec
// ecall; class lookup by name is an INLINED FindClass ecall (the kernel
// class registry, boopsi_registry.c). No out-of-section calls — only
// inline ecalls, indirect dispatcher calls through the IClass Hook, and
// same-section statics (cf. the L4.4 graphics_blit.c marshalling stubs).

#include <cara/sysno.h>
#include <cara/types.h>
#include <exec/lists.h>
#include <exec/memory.h>
#include <exec/nodes.h>
#include <exec/types.h>
#include <intuition/classes.h>
#include <intuition/classusr.h>
#include <utility/hooks.h>

#define LIBTEXT_I __attribute__((section(".lib_text.intuition"), used))

// SetGadgetAttrsA's signature names these; v0 ignores them (the gadget
// refresh path is L8 gadtools), so a forward declaration suffices.
struct Window;
struct Requester;

// The helpers MUST be force-inlined: a real call from the RX page to a
// static in .text would PC-rel-overflow (the L1/L4.4 rule). always_inline
// guarantees they fold into each caller, leaving only the inline ecall /
// indirect dispatch.
#define BOOPSI_INLINE static inline __attribute__((always_inline))

// Inlined shared-heap AllocVec — SYS_AllocVec(size, flags) -> ptr.
BOOPSI_INLINE APTR boopsi_allocvec(ULONG size, ULONG flags)
{
    register long a0 __asm__("a0") = (long)size;
    register long a1 __asm__("a1") = (long)flags;
    register long a7 __asm__("a7") = SYS_AllocVec;
    __asm__ volatile("ecall" : "+r"(a0) : "r"(a1), "r"(a7) : "memory");
    return (APTR)(uptr)a0;
}

// Inlined FreeVec — SYS_FreeVec(ptr).
BOOPSI_INLINE void boopsi_freevec(APTR p)
{
    register long a0 __asm__("a0") = (long)(uptr)p;
    register long a7 __asm__("a7") = SYS_FreeVec;
    __asm__ volatile("ecall" : "+r"(a0) : "r"(a7) : "memory");
}

// Inlined FindClass — SYS_FindClass(name) -> struct IClass * (or null).
BOOPSI_INLINE struct IClass *boopsi_findclass(ClassID id)
{
    register long a0 __asm__("a0") = (long)(uptr)id;
    register long a7 __asm__("a7") = SYS_FindClass;
    __asm__ volatile("ecall" : "+r"(a0) : "r"(a7) : "memory");
    return (struct IClass *)(uptr)a0;
}

// Invoke a class's dispatcher (cl_Dispatcher is at offset 0, so a pointer
// to the class is the Hook pointer the dispatcher receives). Returns IPTR
// (pointer-width): OM_NEW yields an object pointer.
BOOPSI_INLINE IPTR boopsi_dispatch(struct IClass *cl, APTR obj, APTR msg)
{
    return cl->cl_Dispatcher.h_Entry((struct Hook *)cl, obj, msg);
}

// ---- The built-in rootclass dispatcher ------------------------------
//
// Called with hook == the rootclass IClass. On OM_NEW the `obj` argument
// is the TRUE class (NewObjectA passes it), so rootclass sizes the
// allocation from the true class's accumulated instance layout.
LIBTEXT_I IPTR Croi_Boopsi_RootDispatch(struct Hook *hook, APTR obj, APTR msg)
{
    (void)hook;
    ULONG method = ((Msg)msg)->MethodID;
    switch (method) {
    case OM_NEW: {
        struct IClass *trueclass = (struct IClass *)obj;
        ULONG size = (ULONG)sizeof(struct _Object) + (ULONG)trueclass->cl_InstOffset +
                     (ULONG)trueclass->cl_InstSize;
        struct _Object *o = (struct _Object *)boopsi_allocvec(size, MEMF_CLEAR);
        if (!o) {
            return 0;
        }
        o->o_Class = trueclass;
        trueclass->cl_ObjectCount++;
        return (IPTR)BASEOBJECT(o);
    }
    case OM_DISPOSE: {
        struct IClass *cl = OCLASS(obj);
        if (cl) {
            cl->cl_ObjectCount--;
        }
        boopsi_freevec(SHIFTOBJECT(obj));
        return 0;
    }
    case OM_GET: {
        struct opGet *og = (struct opGet *)msg;
        if (og->opg_Storage) {
            *og->opg_Storage = 0;
        }
        return 0;
    }
    case OM_ADDTAIL: {
        // Link this object onto the caller's list (inline AddTail — no
        // out-of-section call). o_Node is at offset 0 of _Object.
        struct opAddTail *oat = (struct opAddTail *)msg;
        struct List *l = oat->opat_List;
        if (l) {
            struct Node *n = (struct Node *)&SHIFTOBJECT(obj)->o_Node;
            n->ln_Succ = (struct Node *)&l->lh_Tail;
            n->ln_Pred = l->lh_TailPred;
            l->lh_TailPred->ln_Succ = n;
            l->lh_TailPred = n;
        }
        return 0;
    }
    case OM_REMOVE: {
        // Unlink this object from whatever list it is on (inline Remove).
        struct Node *n = (struct Node *)&SHIFTOBJECT(obj)->o_Node;
        if (n->ln_Succ && n->ln_Pred) {
            n->ln_Pred->ln_Succ = n->ln_Succ;
            n->ln_Succ->ln_Pred = n->ln_Pred;
        }
        return 0;
    }
    case OM_SET:
    case OM_UPDATE:
    case OM_ADDMEMBER:
    case OM_REMMEMBER:
    default:
        return 0; // base no-op
    }
}

// ---- local LVO impls ------------------------------------------------

// NewObjectA(classPtr, classID, tagList): resolve the class (ptr wins,
// else FindClass by name), then OM_NEW it — passing the true class as the
// "object" so rootclass allocates the right size.
LIBTEXT_I APTR Croi_Intuition_NewObjectA(struct IClass *classPtr, ClassID classID,
                                         struct TagItem *tagList)
{
    struct IClass *cl = classPtr;
    if (!cl && classID) {
        cl = boopsi_findclass(classID);
    }
    if (!cl) {
        return nullptr;
    }
    struct opSet ops = { .MethodID = OM_NEW, .ops_AttrList = tagList, .ops_GInfo = nullptr };
    // boopsi_dispatch returns IPTR (pointer-width); OM_NEW yields the object.
    return (APTR)(uptr)boopsi_dispatch(cl, (APTR)cl, &ops);
}

// DisposeObject(obj): OM_DISPOSE on the object's own class.
LIBTEXT_I void Croi_Intuition_DisposeObject(APTR obj)
{
    if (!obj) {
        return;
    }
    struct IClass *cl = OCLASS(obj);
    if (!cl) {
        return;
    }
    ULONG method = OM_DISPOSE;
    boopsi_dispatch(cl, obj, &method);
}

// MakeClass(classID, superID, superPtr, instSize, flags): build an IClass
// in the shared heap. cl_InstOffset accumulates down the super chain. The
// caller fills cl_Dispatcher after the call (the V36 idiom).
LIBTEXT_I struct IClass *Croi_Intuition_MakeClass(ClassID classID, ClassID superClassID,
                                                  struct IClass *superClassPtr, ULONG instanceSize,
                                                  ULONG flags)
{
    struct IClass *super = superClassPtr;
    if (!super && superClassID) {
        super = boopsi_findclass(superClassID);
    }
    if (!super) {
        return nullptr; // every class needs a superclass (rootclass min.)
    }
    struct IClass *cl = (struct IClass *)boopsi_allocvec((ULONG)sizeof(struct IClass), MEMF_CLEAR);
    if (!cl) {
        return nullptr;
    }
    cl->cl_Super = super;
    cl->cl_ID = classID;
    cl->cl_InstOffset = (UWORD)(super->cl_InstOffset + super->cl_InstSize);
    cl->cl_InstSize = (UWORD)instanceSize;
    cl->cl_Flags = flags;
    super->cl_SubclassCount++;
    return cl;
}

// FreeClass(cl): refuse while objects or subclasses are live; else
// release it and decrement the super's subclass count.
LIBTEXT_I BOOL Croi_Intuition_FreeClass(struct IClass *cl)
{
    if (!cl) {
        return TRUE;
    }
    if (cl->cl_ObjectCount != 0 || cl->cl_SubclassCount != 0) {
        return FALSE;
    }
    if (cl->cl_Super) {
        cl->cl_Super->cl_SubclassCount--;
    }
    boopsi_freevec(cl);
    return TRUE;
}

// SetAttrsA(obj, tags): OM_SET on the object's own class. Returns the
// dispatcher result (a class typically returns the count of attributes
// that need a visual refresh).
LIBTEXT_I ULONG Croi_Intuition_SetAttrsA(APTR obj, struct TagItem *tagList)
{
    if (!obj) {
        return 0;
    }
    struct IClass *cl = OCLASS(obj);
    if (!cl) {
        return 0;
    }
    struct opSet ops = { .MethodID = OM_SET, .ops_AttrList = tagList, .ops_GInfo = nullptr };
    return (ULONG)boopsi_dispatch(cl, obj, &ops);
}

// GetAttr(attrID, obj, storage): OM_GET on the object's own class.
// Returns non-zero if the attribute was gettable (the dispatcher result).
LIBTEXT_I ULONG Croi_Intuition_GetAttr(ULONG attrID, APTR obj, IPTR *storagePtr)
{
    if (!obj) {
        return 0;
    }
    struct IClass *cl = OCLASS(obj);
    if (!cl) {
        return 0;
    }
    struct opGet og = { .MethodID = OM_GET, .opg_AttrID = attrID, .opg_Storage = storagePtr };
    return (ULONG)boopsi_dispatch(cl, obj, &og);
}

// SetGadgetAttrsA(gadget, window, requester, tags): OM_SET on a gadget
// object. v0 dispatches the set; the window/requester visual refresh is
// deferred until gadgetclass bridges BOOPSI to the Leargas gadget
// substrate (L8 gadtools) — ops_GInfo is nullptr for now.
LIBTEXT_I ULONG Croi_Intuition_SetGadgetAttrsA(APTR gadget, struct Window *window,
                                               struct Requester *requester, struct TagItem *tagList)
{
    (void)window;
    (void)requester;
    if (!gadget) {
        return 0;
    }
    struct IClass *cl = OCLASS(gadget);
    if (!cl) {
        return 0;
    }
    struct opSet ops = { .MethodID = OM_SET, .ops_AttrList = tagList, .ops_GInfo = nullptr };
    return (ULONG)boopsi_dispatch(cl, gadget, &ops);
}

// NextObject(objectPtrPtr): step a list iterator over objects linked by
// OM_ADDTAIL. The caller initialises *objectPtrPtr to the list's lh_Head;
// each call returns the current object and advances, or nullptr at the
// tail sentinel (whose ln_Succ is NULL). o_Node is at offset 0 of
// _Object, so a node pointer is the _Object pointer.
LIBTEXT_I APTR Croi_Intuition_NextObject(APTR objectPtrPtr)
{
    if (!objectPtrPtr) {
        return nullptr;
    }
    struct MinNode **statep = (struct MinNode **)objectPtrPtr;
    struct MinNode *cur = *statep;
    if (!cur || cur->mln_Succ == nullptr) {
        return nullptr; // tail sentinel (or empty) — iteration done
    }
    *statep = cur->mln_Succ;
    return BASEOBJECT((struct _Object *)cur);
}
