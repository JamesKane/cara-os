// SPDX-License-Identifier: BSD-2-Clause
//
// Static U-mode hello program. Cross-compiled into its own ELF, embedded
// into croi.elf via .incbin, parsed by the kernel's ELF loader, mapped
// into a fresh user PT, and sret'd into. Compiled freestanding — only
// the syscall ABI is available.

typedef long          long_t;
typedef unsigned long usize_t;

#define SYS_LOG_WRITE 1
#define SYS_EXIT      2
#define LOG_LV_INFO   2

static long_t syscall(long_t n, long_t a0, long_t a1, long_t a2, long_t a3)
{
    register long_t r0 __asm__("a0") = a0;
    register long_t r1 __asm__("a1") = a1;
    register long_t r2 __asm__("a2") = a2;
    register long_t r3 __asm__("a3") = a3;
    register long_t r7 __asm__("a7") = n;
    __asm__ volatile("ecall"
                     : "+r"(r0)
                     : "r"(r1), "r"(r2), "r"(r3), "r"(r7)
                     : "memory");
    return r0;
}

static usize_t my_strlen(const char *s)
{
    usize_t n = 0;
    while (s[n]) {
        n++;
    }
    return n;
}

[[noreturn]] void _start(void);

[[noreturn]] void _start(void)
{
    static const char tag[] = "uelf";
    static const char msg[] = "hello from a real ELF in U-mode";
    syscall(SYS_LOG_WRITE, LOG_LV_INFO, (long_t)tag, (long_t)msg,
            (long_t)my_strlen(msg));
    syscall(SYS_EXIT, 1234, 0, 0, 0);
    for (;;) {
    }
}
