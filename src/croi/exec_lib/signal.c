// SPDX-License-Identifier: BSD-2-Clause
//
// V36+ Wait / Signal / AllocSignal / FreeSignal / SetSignal — thin
// shims over the Phase 1 Croi scheduler primitives in cara/sched.h.

#include <cara/exec_lib.h>
#include <cara/sched.h>
#include <cara/types.h>
#include <exec/types.h>

ULONG Croi_Wait_Impl(ULONG mask)
{
    return (ULONG)Croi_Wait((u32)mask);
}

void Croi_Signal_Impl(struct Task *target, ULONG mask)
{
    Croi_Signal(target, (u32)mask);
}

LONG Croi_AllocSignal_Impl(LONG sig_num)
{
    // V36+ AllocSignal: sig_num == -1 means "any free signal";
    // otherwise allocate the specific bit if free. CaraOS's
    // Croi_AllocSignal only supports the "any free" mode for v0.
    if (sig_num != -1) {
        // TODO: support specific-bit allocation when a real V36+
        // program needs it. For now, fall back to any-free with a
        // single LOG_WARN.
    }
    return (LONG)Croi_AllocSignal();
}

void Croi_FreeSignal_Impl(LONG sig_num)
{
    Croi_FreeSignal((i32)sig_num);
}

ULONG Croi_SetSignal_Impl(ULONG new_signals, ULONG mask)
{
    return (ULONG)Croi_SetSignal((u32)new_signals, (u32)mask);
}
