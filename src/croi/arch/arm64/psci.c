// SPDX-License-Identifier: BSD-2-Clause
//
// AArch64 PSCI (Power State Coordination Interface) — the firmware power seam
// (epic H.7.5, docs/ARM64.md §3). The RISC-V analogue is SBI HSM/SRST; here
// power-off / CPU control go through PSCI calls on the conduit the platform
// advertises in the FDT /psci node ("hvc" or "smc"). On QEMU virt this cleanly
// powers the machine off — the AArch64 counterpart of SBI SYSTEM_RESET.

#include <cara/arch.h>
#include <cara/types.h>

#define PSCI_SYSTEM_OFF 0x84000008u

static bool g_psci_use_hvc = true; // QEMU virt default conduit; set from FDT

// `use_hvc` from the FDT /psci "method" property ("hvc" → true, "smc" → false).
void arm64_psci_init(bool use_hvc);
void arm64_psci_init(bool use_hvc)
{
    g_psci_use_hvc = use_hvc;
}

CARA_NORETURN void arm64_psci_system_off(void);
CARA_NORETURN void arm64_psci_system_off(void)
{
    register u64 x0 __asm__("x0") = PSCI_SYSTEM_OFF;
    if (g_psci_use_hvc) {
        __asm__ volatile("hvc #0" : "+r"(x0) : : "memory");
    } else {
        __asm__ volatile("smc #0" : "+r"(x0) : : "memory");
    }
    // PSCI SYSTEM_OFF does not return; park if the firmware ignored it.
    for (;;) {
        __asm__ volatile("wfi");
    }
}
