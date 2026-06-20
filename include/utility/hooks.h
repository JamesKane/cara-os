// SPDX-License-Identifier: BSD-2-Clause
//
// AmigaOS V36+ utility.library Hook mechanism (3rd Edition RKMs). A
// Hook is a caller-owned callback descriptor: h_Entry is invoked as
//   h_Entry(hook, object, message)
// and may use h_Data for private context. CallHookPkt (utility.library)
// is the canonical dispatcher; BOOPSI (Phase 3 L7) drives its classes
// through Hooks too.
//
// Classic Amiga declared h_Entry as a register-convention `ULONG (*)()`
// with the args arriving in A0/A2/A1. CaraOS is native RV64 with the
// C ABI, so we type the entry as a plain three-argument C function —
// V36+ source that assigns a correctly-typed callback compiles
// unchanged; the asm-stub shim some 68k sources used is unnecessary
// here.

#ifndef UTILITY_HOOKS_H
#define UTILITY_HOOKS_H

#include <exec/nodes.h>
#include <exec/types.h>

struct Hook;

typedef ULONG (*HOOKFUNC)(struct Hook *hook, APTR object, APTR message);

struct Hook {
    struct MinNode h_MinNode;
    HOOKFUNC h_Entry;    // the callback: h_Entry(hook, object, message)
    HOOKFUNC h_SubEntry; // optional secondary entry (caller-defined)
    APTR h_Data;         // caller-private context
};

#endif // UTILITY_HOOKS_H
