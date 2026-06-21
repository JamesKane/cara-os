// SPDX-License-Identifier: BSD-2-Clause
//
// intuition.library wide-LVO marshalling stub (L5 requester). AutoRequest
// takes 8 args — more than the 7-register syscall ABI allows (the
// trampoline needs a7 for the syscall number) — so it's `local`: a thin
// function in the .lib_text.intuition RX page that packs an AutoReqArgs on
// the stack and makes one SYS_AutoRequest ecall. The kernel reads the
// struct with SUM=1 (Croi_AutoRequest_Impl). Same pattern as the L4.4
// graphics blit stubs (src/croi/dath/graphics_blit.c) — self-contained,
// inline ecall, no out-of-section calls.

#include <cara/intuition_lib.h>
#include <cara/sysno.h>
#include <cara/types.h>
#include <exec/types.h>
#include <intuition/intuition.h>

#define LIBTEXT_I __attribute__((section(".lib_text.intuition"), used))

LIBTEXT_I BOOL Croi_AutoRequest(struct Window *window, struct IntuiText *body,
                                struct IntuiText *posText, struct IntuiText *negText,
                                ULONG posFlags, ULONG negFlags, WORD width, WORD height)
{
    struct AutoReqArgs a;
    a.window = window;
    a.body = body;
    a.posText = posText;
    a.negText = negText;
    a.posFlags = posFlags;
    a.negFlags = negFlags;
    a.width = width;
    a.height = height;
    register long a0 __asm__("a0") = (long)(uptr)&a;
    register long a7 __asm__("a7") = SYS_AutoRequest;
    __asm__ volatile("ecall" : "+r"(a0) : "r"(a7) : "memory");
    return (BOOL)a0;
}
