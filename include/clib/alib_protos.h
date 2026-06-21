// SPDX-License-Identifier: BSD-2-Clause
//
// AmigaOS amiga.lib prototypes (`<clib/alib_protos.h>`) — the in-process
// helper functions that ship as a linker library, not as a shared-library
// LVO. CaraOS provides the needed subset in libcara (the U-mode support
// lib), under verbatim names so V36+ source links unmodified.
//
// L7.1 provides the BOOPSI dispatch helpers. The varargs forms
// (DoMethod/DoSuperMethod/CoerceMethod) pack the MethodID + following
// arguments into a small buffer and call the corresponding `…A` form —
// the first word of any method message is its MethodID, so a packed
// {MethodID, arg1, …} is a valid Msg (the BOOPSI contract).
//
// Source: the canonical <clib/alib_protos.h> in the V36+ Includes.

#ifndef CLIB_ALIB_PROTOS_H
#define CLIB_ALIB_PROTOS_H

#include <exec/types.h>
#include <intuition/classusr.h>

// Returns are IPTR (pointer-width) so OM_NEW's object pointer survives on
// 64-bit CaraOS (the classic ULONG would truncate it). The varargs forms
// pack IPTR-width argument words — messages with narrower mixed fields
// must use the `…A` form with the real message struct.

// Dispatch `message` to object `o`'s own class.
IPTR DoMethodA(Object *o, Msg message);
IPTR DoMethod(Object *o, ULONG methodID, ...);

// Dispatch `message` to the superclass of `cl` (the class currently
// handling the method), with the same object.
IPTR DoSuperMethodA(Class *cl, Object *o, Msg message);
IPTR DoSuperMethod(Class *cl, Object *o, ULONG methodID, ...);

// Dispatch `message` to object `o` *as if* it were an instance of the
// explicit class `cl` (used to invoke a specific class's method).
IPTR CoerceMethodA(Class *cl, Object *o, Msg message);
IPTR CoerceMethod(Class *cl, Object *o, ULONG methodID, ...);

#endif // CLIB_ALIB_PROTOS_H
