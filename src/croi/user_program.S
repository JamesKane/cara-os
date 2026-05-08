// SPDX-License-Identifier: BSD-2-Clause
//
// Tiny embedded user program. Lives in croi.elf's .user_text section
// at link time; Croi_SpawnUserTask maps the section's physical bytes
// into a user PT at CARA_USER_TEXT_BASE so this code runs in U-mode.
//
// PC-relative addressing (auipc + addi via `la`) is translation-
// invariant within ±2 GiB, so the user-VA placement doesn't break the
// rodata references.
//
// Calling convention: a7 = syscall number, a0..a5 = args.
//   SYS_LOG_WRITE = 1: a0=level, a1=tag*, a2=msg*, a3=msg_len
//   SYS_EXIT      = 2: a0=status

.section .user_text, "ax"
.balign 16
.globl __user_entry
__user_entry:
    li      a7, 1                // SYS_LOG_WRITE
    li      a0, 2                // LOG_LV_INFO
    la      a1, .Ltag
    la      a2, .Lmsg
    li      a3, 17               // strlen("hello from U-mode")
    ecall

    li      a7, 2                // SYS_EXIT
    li      a0, 42               // status (arbitrary, asserted in test)
    ecall

    // Should never return from SYS_EXIT.
1:  j       1b

.balign 8
.Ltag:
    .ascii "user"
    .byte  0
.balign 8
.Lmsg:
    .ascii "hello from U-mode"
    .byte  0
.size   __user_entry, . - __user_entry
