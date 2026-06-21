// SPDX-License-Identifier: BSD-2-Clause
//
// libcara — BOOPSI dispatch helpers (the amiga.lib analog, L7.1). These
// are in-process pointer-chasing functions, NOT intuition.library LVOs:
// in AmigaOS DoMethod/DoSuperMethod/CoerceMethod ship in amiga.lib, and
// CaraOS folds them into libcara under verbatim names (<clib/alib_protos.h>).
//
// Dispatch invokes the class's Hook: cl_Dispatcher is the first member of
// struct IClass, so a pointer to the class is a pointer to its Hook, and
// the dispatcher is called as h_Entry((struct Hook *)cl, object, message)
// (the BOOPSI contract; OCLASS recovers the class from the object).
//
// The varargs forms copy the MethodID + a bounded number of following
// words into a stack buffer and call the `…A` form. The bound
// (BOOPSI_MAXARGS) covers every standard method; over-reading varargs the
// caller didn't pass is the classic amiga.lib behaviour (the dispatcher
// only consumes the words its message defines).

#include <clib/alib_protos.h>
#include <intuition/classes.h>
#include <stdarg.h>

// MethodID + up to 15 argument words — ample for V36+ method messages.
// Words are IPTR-width: a packed {MethodID, arg, …} matches the C ABI of
// a message struct whose fields are pointer-sized (the 64-bit BOOPSI
// convention). Messages with narrower mixed fields use the `…A` forms.
#define BOOPSI_MAXARGS 16

static inline IPTR boopsi_call(Class *cl, Object *o, Msg message)
{
    return cl->cl_Dispatcher.h_Entry((struct Hook *)cl, (APTR)o, (APTR)message);
}

IPTR DoMethodA(Object *o, Msg message)
{
    if (!o) {
        return 0;
    }
    Class *cl = OCLASS(o);
    if (!cl) {
        return 0;
    }
    return boopsi_call(cl, o, message);
}

IPTR DoSuperMethodA(Class *cl, Object *o, Msg message)
{
    if (!cl || !cl->cl_Super) {
        return 0;
    }
    return boopsi_call(cl->cl_Super, o, message);
}

IPTR CoerceMethodA(Class *cl, Object *o, Msg message)
{
    if (!cl) {
        return 0;
    }
    return boopsi_call(cl, o, message);
}

IPTR DoMethod(Object *o, ULONG methodID, ...)
{
    IPTR buf[BOOPSI_MAXARGS];
    buf[0] = methodID;
    va_list ap;
    va_start(ap, methodID);
    for (int i = 1; i < BOOPSI_MAXARGS; i++) {
        buf[i] = va_arg(ap, IPTR);
    }
    va_end(ap);
    return DoMethodA(o, (Msg)buf);
}

IPTR DoSuperMethod(Class *cl, Object *o, ULONG methodID, ...)
{
    IPTR buf[BOOPSI_MAXARGS];
    buf[0] = methodID;
    va_list ap;
    va_start(ap, methodID);
    for (int i = 1; i < BOOPSI_MAXARGS; i++) {
        buf[i] = va_arg(ap, IPTR);
    }
    va_end(ap);
    return DoSuperMethodA(cl, o, (Msg)buf);
}

IPTR CoerceMethod(Class *cl, Object *o, ULONG methodID, ...)
{
    IPTR buf[BOOPSI_MAXARGS];
    buf[0] = methodID;
    va_list ap;
    va_start(ap, methodID);
    for (int i = 1; i < BOOPSI_MAXARGS; i++) {
        buf[i] = va_arg(ap, IPTR);
    }
    va_end(ap);
    return CoerceMethodA(cl, o, (Msg)buf);
}
