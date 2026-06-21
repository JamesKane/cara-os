// SPDX-License-Identifier: BSD-2-Clause
//
// userintuition — the I3 worked example for intuition.library, the
// twin of userexec.c for exec.library. Demonstrates the full U-mode →
// intuition.library → Leargas chain:
//
//   - libcara's _start has set SysBase by inline-ecall'ing
//     SYS_OpenLibrary("exec.library", 0). main() runs.
//   - main() OpenLibrary("intuition.library", 0) via the <proto/exec.h>
//     inline stub (an exec.library syscall) and assigns the IntuitionBase
//     global the <proto/intuition.h> stubs index off.
//   - main() OpenWindow(&nw) with NewWindow.Screen = nullptr — the
//     <proto/intuition.h> stub loads vec[CARA_IDX_OpenWindow] (a
//     trampoline in the shared 0x4000_0000 RX page) and ecalls; the
//     dispatcher routes SYS_OpenWindow → Croi_OpenWindow_Impl →
//     Leargas_OpenWindow, which opens onto the active (boot/test) screen
//     and returns a SASOS Window pointer valid in U-mode.
//   - main() CloseWindow(w), CloseLibrary(intuition), returns OK;
//     libcara tail-calls SYS_EXIT.
//
// The kernel-side smoke (test_userintuition.c) sets up an active screen
// first (the boot path opens none under -nographic), spawns this ELF,
// and asserts the SYS_EXIT status is USERINT_EXIT_OK.

#include <cara/sysno.h>
#include <clib/alib_protos.h>
#include <exec/execbase.h>
#include <exec/libraries.h>
#include <exec/lists.h>
#include <exec/types.h>
#include <intuition/classes.h>
#include <intuition/classusr.h>
#include <intuition/intuition.h>
#include <intuition/intuitionbase.h>
#include <proto/exec.h>
#include <proto/intuition.h>
#include <utility/hooks.h>
#include <utility/tagitem.h>

#define USERINT_EXIT_OK 0xC1A7
#define USERINT_EXIT_NO_SYSBASE 0xBAD1
#define USERINT_EXIT_NO_INTUITION 0xBAD2
#define USERINT_EXIT_OPENWINDOW_FAILED 0xBAD3
#define USERINT_EXIT_BASE_MISMATCH 0xBAD4
#define USERINT_EXIT_MAKECLASS_FAILED 0xBAD5
#define USERINT_EXIT_NEWOBJECT_FAILED 0xBAD6
#define USERINT_EXIT_OM_NEW_DEFAULT 0xBAD7
#define USERINT_EXIT_DOMETHOD_FAILED 0xBAD8
#define USERINT_EXIT_SUPER_FALLTHROUGH 0xBAD9
#define USERINT_EXIT_FREECLASS_LIVE 0xBADA
#define USERINT_EXIT_FREECLASS_FAILED 0xBADB
#define USERINT_EXIT_SETATTR_FAILED 0xBADC
#define USERINT_EXIT_GETATTR_FAILED 0xBADD
#define USERINT_EXIT_GETATTR_UNKNOWN 0xBADE
#define USERINT_EXIT_SETGADGET_FAILED 0xBADF
#define USERINT_EXIT_NEXTOBJECT_FAILED 0xBAE0
#define USERINT_EXIT_ADDCLASS_PREFAIL 0xBAE1
#define USERINT_EXIT_ADDCLASS_BYNAME 0xBAE2
#define USERINT_EXIT_REMCLASS_FAILED 0xBAE3

// Referenced by the <proto/intuition.h> inline stubs. libcara only
// bootstraps SysBase; IntuitionBase is this program's to set from the
// OpenLibrary return (the V36+ idiom).
struct IntuitionBase *IntuitionBase;

// Inline ecall for SYS_LOG_WRITE — progress markers in the kernel log,
// mirroring userexec.c.
static void log_msg(int level, const char *tag, const char *msg)
{
    long len = 0;
    while (msg[len]) {
        len++;
    }
    register long a0 __asm__("a0") = level;
    register long a1 __asm__("a1") = (long)tag;
    register long a2 __asm__("a2") = (long)msg;
    register long a3 __asm__("a3") = len;
    register long a7 __asm__("a7") = SYS_LOG_WRITE;
    __asm__ volatile("ecall" ::"r"(a0), "r"(a1), "r"(a2), "r"(a3), "r"(a7) : "memory");
}

// ---- BOOPSI exercise (L7.1) -----------------------------------------
//
// A custom class subclassing rootclass, defined entirely in this
// program's own U-mode text — its dispatcher is ordinary user code that
// the BOOPSI machinery (in the intuition.library RX page) calls back
// through the class Hook. Proves MakeClass / NewObject / DoMethod /
// DisposeObject / FreeClass and the DoSuperMethod fall-through to
// rootclass.

struct MyInst {
    ULONG mc_value;
};

#define MCM_GETVALUE 0x401       // custom method: return the stored value
#define MCM_DOUBLE 0x402         // custom method: double it, return new value
#define MYA_Value (TAG_USER + 1) // custom attribute (OM_SET / OM_GET)

static IPTR my_dispatch(struct Hook *hook, APTR obj, APTR msg)
{
    Class *cl = (Class *)hook; // cl_Dispatcher is at offset 0 of IClass
    ULONG method = ((Msg)msg)->MethodID;
    switch (method) {
    case OM_NEW: {
        // Let rootclass allocate, then init our own instance data.
        APTR o = (APTR)(IPTR)DoSuperMethodA(cl, obj, (Msg)msg);
        if (!o) {
            return 0;
        }
        struct MyInst *in = (struct MyInst *)INST_DATA(cl, o);
        in->mc_value = 0x1234;
        return (IPTR)o;
    }
    case OM_SET: {
        // Walk the tag list for MYA_Value; defer the rest to rootclass.
        struct opSet *ops = (struct opSet *)msg;
        struct MyInst *in = (struct MyInst *)INST_DATA(cl, obj);
        ULONG changed = 0;
        for (struct TagItem *ti = ops->ops_AttrList; ti && ti->ti_Tag != TAG_END; ti++) {
            if (ti->ti_Tag == MYA_Value) {
                in->mc_value = (ULONG)ti->ti_Data;
                changed++;
            }
        }
        return changed;
    }
    case OM_GET: {
        struct opGet *og = (struct opGet *)msg;
        if (og->opg_AttrID == MYA_Value) {
            struct MyInst *in = (struct MyInst *)INST_DATA(cl, obj);
            *og->opg_Storage = (IPTR)in->mc_value;
            return 1; // gettable
        }
        // Unknown attribute → rootclass (writes 0, returns 0).
        return DoSuperMethodA(cl, obj, (Msg)msg);
    }
    case MCM_GETVALUE: {
        struct MyInst *in = (struct MyInst *)INST_DATA(cl, obj);
        return (IPTR)in->mc_value;
    }
    case MCM_DOUBLE: {
        struct MyInst *in = (struct MyInst *)INST_DATA(cl, obj);
        in->mc_value *= 2;
        return (IPTR)in->mc_value;
    }
    default:
        // Unknown methods defer to the superclass (rootclass → no-op/0).
        return DoSuperMethodA(cl, obj, (Msg)msg);
    }
}

// Run the BOOPSI exercise; return 0 on success or a USERINT_EXIT_* code.
static int boopsi_exercise(void)
{
    struct IClass *cl = MakeClass((ClassID) "myclass", (ClassID)ROOTCLASS, nullptr,
                                  (ULONG)sizeof(struct MyInst), 0);
    if (!cl) {
        return (int)USERINT_EXIT_MAKECLASS_FAILED;
    }
    cl->cl_Dispatcher.h_Entry = my_dispatch;
    cl->cl_Dispatcher.h_Data = nullptr;

    APTR o = NewObjectA(cl, nullptr, nullptr);
    if (!o) {
        FreeClass(cl);
        return (int)USERINT_EXIT_NEWOBJECT_FAILED;
    }
    // OM_NEW ran our subclass init (0x1234), via DoSuperMethod alloc.
    if (DoMethod(o, MCM_GETVALUE) != 0x1234) {
        DisposeObject(o);
        FreeClass(cl);
        return (int)USERINT_EXIT_OM_NEW_DEFAULT;
    }
    // A custom method that mutates instance state.
    if (DoMethod(o, MCM_DOUBLE) != 0x2468) {
        DisposeObject(o);
        FreeClass(cl);
        return (int)USERINT_EXIT_DOMETHOD_FAILED;
    }
    // An unknown method falls through to rootclass (returns 0).
    if (DoMethod(o, 0x9999) != 0) {
        DisposeObject(o);
        FreeClass(cl);
        return (int)USERINT_EXIT_SUPER_FALLTHROUGH;
    }

    // ---- Attributes (L7.2): SetAttrs / GetAttr round-trip. ----
    struct TagItem set_tags[] = {
        { MYA_Value, 0x5678 },
        { TAG_END, 0 },
    };
    SetAttrsA(o, set_tags);
    IPTR got = 0;
    if (GetAttr(MYA_Value, o, &got) == 0 || got != 0x5678) {
        DisposeObject(o);
        FreeClass(cl);
        return (int)USERINT_EXIT_GETATTR_FAILED;
    }
    // An unknown attribute falls through to rootclass: returns 0, no write.
    IPTR none = 0xDEAD;
    if (GetAttr(0x4242, o, &none) != 0 || none != 0) {
        DisposeObject(o);
        FreeClass(cl);
        return (int)USERINT_EXIT_GETATTR_UNKNOWN;
    }
    // SetGadgetAttrsA dispatches OM_SET like SetAttrs (no gadget refresh
    // in v0); prove it mutates the attribute too.
    struct TagItem gad_tags[] = {
        { MYA_Value, 0x0BAD },
        { TAG_END, 0 },
    };
    SetGadgetAttrsA((APTR)o, nullptr, nullptr, gad_tags);
    if (GetAttr(MYA_Value, o, &got) == 0 || got != 0x0BAD) {
        DisposeObject(o);
        FreeClass(cl);
        return (int)USERINT_EXIT_SETGADGET_FAILED;
    }

    // ---- Object lists (L7.2): OM_ADDTAIL + NextObject. ----
    struct MinList list = { 0 };
    list.mlh_Head = (struct MinNode *)&list.mlh_Tail;
    list.mlh_Tail = nullptr;
    list.mlh_TailPred = (struct MinNode *)&list.mlh_Head;
    APTR members[3];
    for (int i = 0; i < 3; i++) {
        members[i] = NewObjectA(cl, nullptr, nullptr);
        if (!members[i]) {
            return (int)USERINT_EXIT_NEWOBJECT_FAILED;
        }
        DoMethod(members[i], OM_ADDTAIL, (IPTR)&list);
    }
    // Walk: expect exactly our three objects, in insertion order.
    int count = 0;
    APTR state = (APTR)list.mlh_Head;
    APTR walked;
    while ((walked = NextObject(&state)) != nullptr) {
        if (count < 3 && walked != members[count]) {
            // out-of-order / wrong object
            count = -100;
            break;
        }
        count++;
    }
    if (count != 3) {
        for (int i = 0; i < 3; i++) {
            DoMethod(members[i], OM_REMOVE);
            DisposeObject(members[i]);
        }
        DisposeObject(o);
        FreeClass(cl);
        return (int)USERINT_EXIT_NEXTOBJECT_FAILED;
    }
    // Remove the middle one; the walk now yields two.
    DoMethod(members[1], OM_REMOVE);
    count = 0;
    state = (APTR)list.mlh_Head;
    while ((walked = NextObject(&state)) != nullptr) {
        count++;
    }
    if (count != 2) {
        for (int i = 0; i < 3; i++) {
            DisposeObject(members[i]);
        }
        DisposeObject(o);
        FreeClass(cl);
        return (int)USERINT_EXIT_NEXTOBJECT_FAILED;
    }
    // Tear down the remaining members.
    DoMethod(members[0], OM_REMOVE);
    DoMethod(members[2], OM_REMOVE);
    for (int i = 0; i < 3; i++) {
        DisposeObject(members[i]);
    }

    // ---- Public class registry (L7.3): AddClass / NewObject-by-name. ----
    // Before publishing, NewObject by name must NOT resolve.
    APTR byname = NewObjectA(nullptr, (ClassID) "myclass", nullptr);
    if (byname) {
        DisposeObject(byname);
        DisposeObject(o);
        FreeClass(cl);
        return (int)USERINT_EXIT_ADDCLASS_PREFAIL;
    }
    // Publish it; now NewObject by name resolves via FindClass.
    AddClass(cl);
    byname = NewObjectA(nullptr, (ClassID) "myclass", nullptr);
    if (!byname || DoMethod(byname, MCM_GETVALUE) != 0x1234) {
        if (byname) {
            DisposeObject(byname);
        }
        RemoveClass(cl);
        DisposeObject(o);
        FreeClass(cl);
        return (int)USERINT_EXIT_ADDCLASS_BYNAME;
    }
    DisposeObject(byname);
    // Unpublish; by-name resolution must fail again.
    RemoveClass(cl);
    byname = NewObjectA(nullptr, (ClassID) "myclass", nullptr);
    if (byname) {
        DisposeObject(byname);
        DisposeObject(o);
        FreeClass(cl);
        return (int)USERINT_EXIT_REMCLASS_FAILED;
    }

    // FreeClass must refuse while the object is still live.
    if (FreeClass(cl)) {
        DisposeObject(o);
        return (int)USERINT_EXIT_FREECLASS_LIVE;
    }
    DisposeObject(o);
    // Now it frees cleanly.
    if (!FreeClass(cl)) {
        return (int)USERINT_EXIT_FREECLASS_FAILED;
    }
    return 0;
}

int main(void);

int main(void)
{
    log_msg(2, "uintu", "userintuition entered");

    if (!SysBase) {
        return (int)USERINT_EXIT_NO_SYSBASE;
    }

    // 1. Open intuition.library via the exec.library OpenLibrary stub.
    struct Library *ilib = OpenLibrary((STRPTR) "intuition.library", 0);
    if (!ilib) {
        return (int)USERINT_EXIT_NO_INTUITION;
    }
    IntuitionBase = (struct IntuitionBase *)ilib;
    if (IntuitionBase->LibNode.lib_Version != 36) {
        CloseLibrary(ilib);
        return (int)USERINT_EXIT_BASE_MISMATCH;
    }

    // 2. OpenWindow on the active screen (Screen = nullptr). IDCMPFlags
    //    zero keeps it UserPort-free — this smoke proves the LVO chain,
    //    not the IDCMP path. The returned Window is a shared-heap SASOS
    //    pointer, dereferenceable here in U-mode.
    char title[] = "Clar";
    struct NewWindow nw = {
        .LeftEdge = 20,
        .TopEdge = 20,
        .Width = 120,
        .Height = 60,
        .DetailPen = 0,
        .BlockPen = 1,
        .IDCMPFlags = 0,
        .Flags = WFLG_DRAGBAR | WFLG_DEPTHGADGET | WFLG_CLOSEGADGET,
        .FirstGadget = nullptr,
        .Title = (UBYTE *)title,
        .Screen = nullptr,
    };
    struct Window *w = OpenWindow(&nw);
    if (!w) {
        CloseLibrary(ilib);
        return (int)USERINT_EXIT_OPENWINDOW_FAILED;
    }

    // 3. Exercise the BOOPSI class system (L7.1).
    int boopsi_rc = boopsi_exercise();
    if (boopsi_rc != 0) {
        CloseWindow(w);
        CloseLibrary(ilib);
        return boopsi_rc;
    }

    // 4. Tear down and balance the open.
    CloseWindow(w);
    CloseLibrary(ilib);

    log_msg(2, "uintu", "userintuition ok");
    return (int)USERINT_EXIT_OK;
}
