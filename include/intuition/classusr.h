// SPDX-License-Identifier: BSD-2-Clause
//
// AmigaOS V36+ intuition/classusr.h — the BOOPSI types and method/
// attribute message shapes every class *user* (and class author) targets
// (`<intuition/classusr.h>`). Verbatim names, method IDs, and message
// field offsets are ABI so V36+ source compiles unmodified.
//
// Source: AmigaOS RKM Libraries 3rd Edition (Boopsi) and the canonical
// <intuition/classusr.h> in the V36+ Includes.

#ifndef INTUITION_CLASSUSR_H
#define INTUITION_CLASSUSR_H

#include <exec/types.h>
#include <utility/tagitem.h>

// A class is named by a NUL-terminated string ("rootclass",
// "gadgetclass", …) or, for a private class, nullptr.
typedef STRPTR ClassID;

// An object handle. Programs declare `Object *obj` and pass it to the
// BOOPSI calls; `Object *` is the canonical object pointer.
typedef void Object;

// The class object (full layout in <intuition/classes.h>).
typedef struct IClass Class;

// Every method message begins with a method id; a Msg is the generic
// view of one.
typedef struct {
    ULONG MethodID;
} *Msg;

// Built-in (rootclass) method ids — verbatim values.
#define OM_Dummy 0x100
#define OM_NEW (OM_Dummy + 0x01)       // 0x101 — create an object
#define OM_DISPOSE (OM_Dummy + 0x02)   // 0x102 — delete an object
#define OM_SET (OM_Dummy + 0x03)       // 0x103 — set attributes
#define OM_GET (OM_Dummy + 0x04)       // 0x104 — get one attribute
#define OM_ADDTAIL (OM_Dummy + 0x05)   // 0x105 — add to an exec list
#define OM_REMOVE (OM_Dummy + 0x06)    // 0x106 — remove from its list
#define OM_NOTIFY (OM_Dummy + 0x07)    // 0x107 — notify (icclass)
#define OM_UPDATE (OM_Dummy + 0x08)    // 0x108 — receive a notification
#define OM_ADDMEMBER (OM_Dummy + 0x09) // 0x109 — add a member object
#define OM_REMMEMBER (OM_Dummy + 0x0A) // 0x10A — remove a member object

// Well-known class names.
#define ROOTCLASS "rootclass"

// OM_NEW / OM_SET message.
struct opSet {
    ULONG MethodID;
    struct TagItem *ops_AttrList;
    struct GadgetInfo *ops_GInfo;
};

// OM_UPDATE message (notification delivery).
struct opUpdate {
    ULONG MethodID;
    struct TagItem *opu_AttrList;
    struct GadgetInfo *opu_GInfo;
    ULONG opu_Flags;
};
#define OPUF_INTERIM 0x01

// OM_GET message. opg_Storage is IPTR* (pointer-width), not the classic
// ULONG*, so a class can return a pointer-valued attribute on 64-bit
// CaraOS without truncation (the AROS 64-bit convention).
struct opGet {
    ULONG MethodID;
    ULONG opg_AttrID;
    IPTR *opg_Storage;
};

// OM_ADDTAIL / OM_REMOVE message (exec-list membership).
struct opAddTail {
    ULONG MethodID;
    struct List *opat_List;
};

// OM_ADDMEMBER / OM_REMMEMBER message (object membership).
struct opMember {
    ULONG MethodID;
    Object *opam_Object;
};

#endif // INTUITION_CLASSUSR_H
