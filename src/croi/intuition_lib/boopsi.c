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
#include <exec/memory.h>
#include <exec/types.h>
#include <intuition/classes.h>
#include <intuition/classusr.h>
#include <utility/hooks.h>

#define LIBTEXT_I __attribute__((section(".lib_text.intuition"), used))

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
