// SPDX-License-Identifier: BSD-2-Clause
//
// BOOPSI public class registry (L7.1, docs/LEARGAS_BOOPSI.md §2.5) —
// kernel-side, mirroring the L6 device registry. Named public classes are
// findable by name from any task; the IClass structs themselves live in
// the SASOS shared heap (built by the U-mode MakeClass), so the registry
// only stores pointers and reads cl_ID with SUM=1.
//
// L7.1 registers the built-in rootclass at boot (Croi_Boopsi_Init) and
// serves FindClass (the syscall NewObjectA/MakeClass use to resolve a
// class by name). AddClass/RemoveClass (publishing user classes) arrive
// in L7.3 over Croi_Boopsi_RegisterClass / _UnregisterClass.

#include <cara/intuition_lib.h>
#include <cara/shared.h>
#include <cara/types.h>
#include <exec/types.h>
#include <intuition/classes.h>
#include <intuition/classusr.h>
#include <utility/hooks.h>

#define BOOPSI_MAX_CLASSES 32

static struct IClass *g_classes[BOOPSI_MAX_CLASSES];
static u32 g_class_count;

// The built-in rootclass IClass (shared heap, so its Hook is reachable by
// U-mode dispatch). Built once at boot.
static struct IClass *g_rootclass;

// The rootclass dispatcher lives in .lib_text.intuition (U-mode RX page),
// ~1 GiB from kernel .text — too far for a PC-relative address load. Take
// its address through a static initializer (an absolute R_RISCV_64 reloc,
// full range), then load that pointer at runtime like the generated vec
// table does for the trampolines.
// volatile so the compiler can't constant-fold the initializer back into a
// PC-relative address-take in Croi_Boopsi_Init — it must load the pointer
// from this .data slot (filled by the absolute reloc) at runtime.
extern IPTR Croi_Boopsi_RootDispatch(struct Hook *hook, APTR obj, APTR msg);
static HOOKFUNC volatile g_root_dispatcher = (HOOKFUNC)Croi_Boopsi_RootDispatch;

static bool class_name_eq(const char *a, const char *b)
{
    if (!a || !b) {
        return false;
    }
    while (*a && *b) {
        if (*a != *b) {
            return false;
        }
        a++;
        b++;
    }
    return *a == *b;
}

void Croi_Boopsi_RegisterClass(struct IClass *cl)
{
    if (!cl || !cl->cl_ID) {
        return;
    }
    if (Croi_FindClass_Impl((STRPTR)cl->cl_ID)) {
        return; // skip duplicates by name (idempotent), like devices
    }
    if (g_class_count >= BOOPSI_MAX_CLASSES) {
        return;
    }
    g_classes[g_class_count++] = cl;
}

void Croi_Boopsi_UnregisterClass(struct IClass *cl)
{
    for (u32 i = 0; i < g_class_count; i++) {
        if (g_classes[i] == cl) {
            g_classes[i] = g_classes[--g_class_count];
            g_classes[g_class_count] = nullptr;
            return;
        }
    }
}

struct IClass *Croi_FindClass_Impl(STRPTR classID)
{
    if (!classID) {
        return nullptr;
    }
    for (u32 i = 0; i < g_class_count; i++) {
        if (class_name_eq((const char *)g_classes[i]->cl_ID, (const char *)classID)) {
            return g_classes[i];
        }
    }
    return nullptr;
}

// Build + register the built-in rootclass at boot.
void Croi_Boopsi_Init(void)
{
    if (g_rootclass) {
        return; // idempotent
    }
    struct IClass *rc = (struct IClass *)Croi_AllocShared(sizeof(struct IClass));
    if (!rc) {
        return;
    }
    *rc = (struct IClass){ 0 };
    rc->cl_Dispatcher.h_Entry = g_root_dispatcher;
    rc->cl_ID = (ClassID)(uptr)ROOTCLASS;
    rc->cl_Super = nullptr;
    rc->cl_InstOffset = 0;
    rc->cl_InstSize = 0; // rootclass holds no own instance data in v0
    g_rootclass = rc;
    Croi_Boopsi_RegisterClass(rc);
}
