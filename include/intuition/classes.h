// SPDX-License-Identifier: BSD-2-Clause
//
// AmigaOS V36+ intuition/classes.h — the BOOPSI class & object layout
// every class *author* targets (`<intuition/classes.h>`): struct IClass
// (the class object), struct _Object (the hidden per-object prefix), and
// the canonical OCLASS / INST_DATA / SIZEOF_INSTANCE macros. Field order
// and offsets are ABI.
//
// Source: AmigaOS RKM Libraries 3rd Edition (Boopsi) and the canonical
// <intuition/classes.h> in the V36+ Includes.

#ifndef INTUITION_CLASSES_H
#define INTUITION_CLASSES_H

#include <exec/nodes.h>
#include <exec/types.h>
#include <intuition/classusr.h>
#include <utility/hooks.h>

// The class object. cl_Dispatcher is FIRST so a pointer to the IClass is
// also a pointer to its Hook — the dispatcher is invoked as
// cl_Dispatcher.h_Entry((struct Hook *)cl, object, message).
struct IClass {
    struct Hook cl_Dispatcher; // the method dispatcher (offset 0)
    ULONG cl_Reserved;         // reserved; keep zero
    struct IClass *cl_Super;   // superclass (rootclass at the top)
    ClassID cl_ID;             // public name, or nullptr if private
    UWORD cl_InstOffset;       // offset of this class's instance data
    UWORD cl_InstSize;         // size of this class's own instance data
    APTR cl_UserData;          // class-author private
    ULONG cl_SubclassCount;    // live subclasses (FreeClass guard)
    ULONG cl_ObjectCount;      // live objects (FreeClass guard)
    ULONG cl_Flags;            // CLF_* (none defined in v0)
};

// The hidden prefix that precedes every object's instance data.
struct _Object {
    struct MinNode o_Node;  // links the object on a class/parent list
    struct IClass *o_Class; // the object's class — see OCLASS()
};

// Object <-> _Object navigation (verbatim BOOPSI macros).
#define _OBJ(o) ((struct _Object *)(o))
#define SHIFTOBJECT(o) (((struct _Object *)(o)) - 1)            // object -> header
#define BASEOBJECT(o) ((Object *)(((struct _Object *)(o)) + 1)) // header -> object
#define OCLASS(o) (SHIFTOBJECT(o)->o_Class)
#define INST_DATA(cl, o) ((APTR)(((UBYTE *)(o)) + (cl)->cl_InstOffset))
#define SIZEOF_INSTANCE(cl)                                                                        \
    ((ULONG)((cl)->cl_InstOffset + (cl)->cl_InstSize) + (ULONG)sizeof(struct _Object))
#define BASEOBJECT_SIZE ((ULONG)sizeof(struct _Object))

#endif // INTUITION_CLASSES_H
