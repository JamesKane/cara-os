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
#include <cara/usb.h>
#include <cara/xhci.h>

extern struct XhciController g_xhci;
extern bool g_xhci_probed;

KERNEL_TEST(xhci_smoke)
{
    TEST_ASSERT(ctx, g_xhci_probed, "Croi_Xhci_Probe did not run successfully at boot");

    // QEMU's qemu-xhci is xHCI 1.0; HCIVERSION encodes that as
    // 0x0100 (BCD-style major.minor in the high two bytes).
    TEST_ASSERT(ctx, g_xhci.hci_version >= 0x0100, "HCIVERSION < 0x100 (pre-xHCI 1.0)");

    TEST_ASSERT(ctx, g_xhci.max_slots > 0, "HCSPARAMS1.MaxSlots == 0");
    TEST_ASSERT(ctx, g_xhci.max_ports > 0, "HCSPARAMS1.MaxPorts == 0");
    TEST_ASSERT(ctx, g_xhci.bar0_phys != 0, "BAR0 not allocated");
    TEST_ASSERT(ctx, g_xhci.cap_regs != nullptr, "cap_regs not mapped");
    TEST_ASSERT(ctx, g_xhci.op_regs > g_xhci.cap_regs, "op_regs offset (CAPLENGTH) zero");

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

    // UB.5 assertion: each connected device went through Port Reset →
    // Enable Slot → Address Device. The Output Slot Context's Slot
    // State should now read Addressed (or higher) for every used slot,
    // and the assigned USB Device Address must be non-zero.
    TEST_ASSERT(ctx, g_xhci.n_addressed_slots >= 2,
                "expected >= 2 addressed slots (usb-kbd + usb-mouse)");
    bool found_addressed = false;
    for (u32 sid = 1; sid <= CARA_XHCI_MAX_SLOTS; sid++) {
        if (!g_xhci.slots[sid].in_use) {
            continue;
        }
        TEST_ASSERT(ctx, g_xhci.slots[sid].slot_state >= XHCI_SLOT_STATE_ADDRESSED,
                    "slot did not transition to Addressed");
        TEST_ASSERT(ctx, g_xhci.slots[sid].usb_address != 0,
                    "Slot Context USB Device Address still zero");
        TEST_ASSERT(ctx, g_xhci.dcbaa[sid] != 0, "DCBAA entry not installed for in-use slot");
        found_addressed = true;
    }
    TEST_ASSERT(ctx, found_addressed, "no slot reached Addressed state");

    // UC.1 assertions: each in-use slot has a valid 18-byte USB Device
    // Descriptor read back over EP0. Structural sanity only — VID/PID
    // values vary across QEMU versions, but the spec-defined fields
    // (bLength, bDescriptorType, bMaxPacketSize0, bNumConfigurations)
    // must hold for every conformant device.
    TEST_ASSERT(ctx, g_xhci.n_described_slots >= 2,
                "expected >= 2 slots with device descriptors read");
    for (u32 sid = 1; sid <= CARA_XHCI_MAX_SLOTS; sid++) {
        if (!g_xhci.slots[sid].in_use) {
            continue;
        }
        TEST_ASSERT(ctx, g_xhci.slots[sid].device_descriptor.valid,
                    "in-use slot missing device descriptor");
        const struct UsbDeviceDescriptor *d =
            (const struct UsbDeviceDescriptor *)g_xhci.slots[sid].device_descriptor.raw;
        TEST_ASSERT(ctx, d->bLength == USB_DEVICE_DESCRIPTOR_BYTES,
                    "device descriptor bLength != 18");
        TEST_ASSERT(ctx, d->bDescriptorType == USB_DT_DEVICE,
                    "device descriptor bDescriptorType != DEVICE");
        TEST_ASSERT(ctx, d->bNumConfigurations >= 1, "device reports zero configurations");
        TEST_ASSERT(ctx,
                    d->bMaxPacketSize0 == 8 || d->bMaxPacketSize0 == 16 ||
                        d->bMaxPacketSize0 == 32 || d->bMaxPacketSize0 == 64,
                    "device EP0 bMaxPacketSize0 not in {8,16,32,64}");
    }

    // UC.2 assertions: every described slot has a parsed configuration
    // with at least one HID interface; collectively across the two
    // attached devices we should see both keyboard (protocol=1) and
    // mouse (protocol=2) HID/Boot interfaces, each carrying an
    // interrupt-IN endpoint.
    TEST_ASSERT(ctx, g_xhci.n_configured_slots >= 2,
                "expected >= 2 slots with configurations parsed");
    bool seen_kbd = false, seen_mouse = false;
    for (u32 sid = 1; sid <= CARA_XHCI_MAX_SLOTS; sid++) {
        if (!g_xhci.slots[sid].in_use) {
            continue;
        }
        TEST_ASSERT(ctx, g_xhci.slots[sid].configuration_descriptor.valid,
                    "in-use slot missing configuration descriptor");
        TEST_ASSERT(ctx, g_xhci.slots[sid].configuration_descriptor.bConfigurationValue != 0,
                    "configuration value not parsed");
        TEST_ASSERT(ctx, g_xhci.slots[sid].n_interfaces >= 1,
                    "no interfaces parsed for in-use slot");
        for (u32 j = 0; j < g_xhci.slots[sid].n_interfaces; j++) {
            const auto iface = &g_xhci.slots[sid].interfaces[j];
            if (iface->bInterfaceClass != USB_CLASS_HID) {
                continue;
            }
            if (iface->bInterfaceSubClass != USB_HID_SUBCLASS_BOOT) {
                continue;
            }
            TEST_ASSERT(ctx, iface->ep_present, "HID interface has no interrupt-IN endpoint");
            TEST_ASSERT(ctx, (iface->ep_address & USB_EP_DIR_IN) != 0,
                        "HID interrupt endpoint not IN-direction");
            if (iface->bInterfaceProtocol == USB_HID_PROTOCOL_KEYBOARD) {
                seen_kbd = true;
            } else if (iface->bInterfaceProtocol == USB_HID_PROTOCOL_MOUSE) {
                seen_mouse = true;
            }
        }
    }
    TEST_ASSERT(ctx, seen_kbd, "no HID/Boot/Keyboard interface found across all slots");
    TEST_ASSERT(ctx, seen_mouse, "no HID/Boot/Mouse interface found across all slots");

    // UC.3 assertions: every parsed-config slot has been driven through
    // SET_CONFIGURATION over EP0 and reports usb_configured = true.
    TEST_ASSERT(ctx, g_xhci.n_usb_configured_slots >= 2,
                "expected >= 2 slots with USB SET_CONFIGURATION applied");
    for (u32 sid = 1; sid <= CARA_XHCI_MAX_SLOTS; sid++) {
        if (!g_xhci.slots[sid].in_use) {
            continue;
        }
        if (!g_xhci.slots[sid].configuration_descriptor.valid) {
            continue;
        }
        TEST_ASSERT(ctx, g_xhci.slots[sid].usb_configured,
                    "in-use slot did not receive SET_CONFIGURATION");
    }

    // UC.4 assertions: classification populated dispatch on every
    // parsed interface; collectively across slots we see exactly one
    // boot keyboard and one boot mouse (matching the QEMU usb-kbd /
    // usb-mouse pair). Every interface should have its dispatch field
    // resolved away from XHCI_HID_NONE.
    TEST_ASSERT(ctx, g_xhci.n_hid_keyboards >= 1, "expected >= 1 dispatched HID/Boot/Keyboard");
    TEST_ASSERT(ctx, g_xhci.n_hid_mice >= 1, "expected >= 1 dispatched HID/Boot/Mouse");
    for (u32 sid = 1; sid <= CARA_XHCI_MAX_SLOTS; sid++) {
        if (!g_xhci.slots[sid].in_use) {
            continue;
        }
        for (u32 j = 0; j < g_xhci.slots[sid].n_interfaces; j++) {
            const auto iface = &g_xhci.slots[sid].interfaces[j];
            if (!iface->valid) {
                continue;
            }
            TEST_ASSERT(ctx, iface->dispatch != XHCI_HID_NONE,
                        "valid interface left at XHCI_HID_NONE dispatch");
        }
    }

    // UC.5 assertions: every HID-dispatched interface ran the
    // Configure Endpoint Command and has a per-EP Transfer Ring; the
    // owning slot transitioned to xHCI Configured state (3).
    TEST_ASSERT(ctx, g_xhci.n_xhci_configured_interfaces >= 2,
                "expected >= 2 xHCI-configured HID interrupt-IN endpoints");
    for (u32 sid = 1; sid <= CARA_XHCI_MAX_SLOTS; sid++) {
        if (!g_xhci.slots[sid].in_use) {
            continue;
        }
        bool any_hid = false;
        for (u32 j = 0; j < g_xhci.slots[sid].n_interfaces; j++) {
            const auto iface = &g_xhci.slots[sid].interfaces[j];
            if (iface->dispatch != XHCI_HID_KEYBOARD && iface->dispatch != XHCI_HID_MOUSE) {
                continue;
            }
            any_hid = true;
            TEST_ASSERT(ctx, iface->ep_xhci_configured,
                        "HID interface failed Configure Endpoint Command");
            TEST_ASSERT(ctx, iface->int_ep_dci >= 2,
                        "HID interface DCI < 2 (collides with EP0/Slot)");
            TEST_ASSERT(ctx, iface->int_ring_phys != 0, "HID interface int-IN ring not allocated");
            TEST_ASSERT(ctx, iface->int_ring != nullptr, "HID interface int-IN ring kva null");
            TEST_ASSERT(ctx, iface->int_buf_phys != 0,
                        "HID interface int-IN scratch buffer not allocated");
            TEST_ASSERT(ctx, iface->int_buf != nullptr,
                        "HID interface int-IN scratch buffer kva null");
        }
        if (any_hid) {
            TEST_ASSERT(ctx, g_xhci.slots[sid].slot_state == XHCI_SLOT_STATE_CONFIGURED,
                        "HID slot did not transition to Configured");
        }
    }

    // HA.1 assertions: every HID-dispatched interface accepted
    // SET_PROTOCOL(Boot) and is now pinned to the canonical 8-byte
    // report layout.
    for (u32 sid = 1; sid <= CARA_XHCI_MAX_SLOTS; sid++) {
        if (!g_xhci.slots[sid].in_use) {
            continue;
        }
        for (u32 j = 0; j < g_xhci.slots[sid].n_interfaces; j++) {
            const auto iface = &g_xhci.slots[sid].interfaces[j];
            if (iface->dispatch != XHCI_HID_KEYBOARD && iface->dispatch != XHCI_HID_MOUSE) {
                continue;
            }
            TEST_ASSERT(ctx, iface->boot_protocol_set,
                        "HID interface SET_PROTOCOL(Boot) did not succeed");
        }
    }

    LOG_INFO("xhcsm",
             "qemu-xhci at %x: v0x%x slots=%u ports=%u connected=%u addressed=%u described=%u "
             "configured=%u usbcfg=%u",
             ((u32)g_xhci.pci_bus << 16) | ((u32)g_xhci.pci_device << 8) | g_xhci.pci_function,
             (unsigned)g_xhci.hci_version, (unsigned)g_xhci.max_slots, (unsigned)g_xhci.max_ports,
             (unsigned)g_xhci.n_connected_ports, (unsigned)g_xhci.n_addressed_slots,
             (unsigned)g_xhci.n_described_slots, (unsigned)g_xhci.n_configured_slots,
             (unsigned)g_xhci.n_usb_configured_slots);
}
