// SPDX-License-Identifier: BSD-2-Clause
//
// TEMPORARY (epic H.7.6). Instantiates the factored, CARA_ARCH-selected
// CARA_SYSCALL_TRAMPOLINE macro for AArch64 to prove it emits a working svc
// trampoline (boot.c calls Cara_DemoEchoTrampoline and checks the round-trip).
// This is the SAME macro the per-library trampolines.S use; the real per-LVO
// trampolines arrive when those libraries build for arm64 (H.7.7+), at which
// point this demo can go.

#include <cara/arch/syscall_trampoline.inc>

.section .text, "ax"
.balign 4

// Demo "syscall" number 0x42: the demo dispatcher (Croi_Syscall_Dispatch in
// boot.c) echoes num + arg0, so Cara_DemoEchoTrampoline(0x100) returns 0x142.
CARA_SYSCALL_TRAMPOLINE Cara_DemoEchoTrampoline, 0x42
