// SPDX-License-Identifier: BSD-2-Clause
//
// KERNEL_TEST(xhci_smoke): asserts the boot-time xHCI probe
// (Croi_Xhci_Probe in entry.c) found a controller, parsed its
// capability registers to sane values, and ran the USBCMD.HCRST
// reset sequence to completion.
//
// Runs on QEMU virt with `-device qemu-xhci -device usb-kbd
// -device usb-mouse` (smoke_qemu_kernel.sh); the qemu-xhci device
// implements xHCI 1.0 so HCIVERSION reads 0x0100 and slot/port
// counts come from QEMU's defaults.

#include <cara/log.h>
#include <cara/test.h>
#include <cara/types.h>
#include <cara/xhci.h>

extern struct XhciController g_xhci;
extern bool                  g_xhci_probed;

KERNEL_TEST(xhci_smoke)
{
    TEST_ASSERT(ctx, g_xhci_probed,
                "Croi_Xhci_Probe did not run successfully at boot");

    // QEMU's qemu-xhci is xHCI 1.0; HCIVERSION encodes that as
    // 0x0100 (BCD-style major.minor in the high two bytes).
    TEST_ASSERT(ctx, g_xhci.hci_version >= 0x0100,
                "HCIVERSION < 0x100 (pre-xHCI 1.0)");

    TEST_ASSERT(ctx, g_xhci.max_slots > 0,
                "HCSPARAMS1.MaxSlots == 0");
    TEST_ASSERT(ctx, g_xhci.max_ports > 0,
                "HCSPARAMS1.MaxPorts == 0");
    TEST_ASSERT(ctx, g_xhci.bar0_phys != 0,
                "BAR0 not allocated");
    TEST_ASSERT(ctx, g_xhci.cap_regs != nullptr,
                "cap_regs not mapped");
    TEST_ASSERT(ctx, g_xhci.op_regs > g_xhci.cap_regs,
                "op_regs offset (CAPLENGTH) zero");

    // Setup-stage assertions: Croi_Xhci_Setup ran in entry.c after
    // Probe, allocated DCBAA / Command Ring / Event Ring / ERST,
    // programmed the controller, and transitioned Run/Stop = 1.
    TEST_ASSERT(ctx, g_xhci.running,
                "controller did not transition to running (USBSTS.HCH still set)");
    TEST_ASSERT(ctx, g_xhci.dcbaa_phys != 0, "DCBAA not allocated");
    TEST_ASSERT(ctx, g_xhci.cmd_ring_phys != 0, "Command Ring not allocated");
    TEST_ASSERT(ctx, g_xhci.event_ring_phys != 0, "Event Ring not allocated");
    TEST_ASSERT(ctx, g_xhci.erst_phys != 0, "ERST not allocated");

    // PORTSC walk assertion. The smoke harness attaches a usb-kbd
    // and a usb-mouse to qemu-xhci; once the controller is running
    // both devices show up as Connected on USB2 root-hub ports.
    TEST_ASSERT(ctx, g_xhci.n_connected_ports >= 2,
                "expected >= 2 connected ports (usb-kbd + usb-mouse)");

    LOG_INFO("xhcsm",
             "qemu-xhci running at %x: v0x%x slots=%u ports=%u connected=%u",
             ((u32)g_xhci.pci_bus << 16) | ((u32)g_xhci.pci_device << 8)
                 | g_xhci.pci_function,
             (unsigned)g_xhci.hci_version,
             (unsigned)g_xhci.max_slots,
             (unsigned)g_xhci.max_ports,
             (unsigned)g_xhci.n_connected_ports);
}
