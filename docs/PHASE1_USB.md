# Phase 1 Subgoal 5 — USB host stack

> Plan for the USB stack that delivers mouse and keyboard events
> to Leargas (Subgoal 6) and ultimately into Clar (Subgoal 7). Pairs
> with `docs/ROADMAP.md` Phase 1 Subgoals 5–7,
> `docs/PHASE1_RUNTIME.md` (the Croi runtime substrate), and
> `docs/HARDWARE_RV2.md` (where on the X1 the USB controller sits).

---

## Status — 2026-05-08

**Tier 1 shipped; Tier 2 begun (UC.1).** Epic UA + UB shipped: the
boot path discovers the xHCI controller via PCIe, brings it through
HCRST, programs DCBAA + Command Ring + Event Ring + the Run/Stop
transition, walks PORTSC, port-resets each connected USB2 port, and
runs Enable Slot + Address Device for each. Both `usb-kbd` and
`usb-mouse` reach the Addressed state under QEMU.

**UC.1 shipped.** First USB control transfer over EP0:
`Croi_Xhci_ControlTransfer` builds Setup / Data / Status TRBs on the
per-slot EP0 transfer ring, rings the slot doorbell at DCI=1, and
polls the event ring for the matching Transfer Event. Wrapped as
`Croi_Xhci_GetDeviceDescriptor` for the 18-byte GET_DESCRIPTOR(DEVICE)
read.

**UC.2 shipped.** `Croi_Xhci_GetConfigurationDescriptor` does the
canonical short-then-full read pair: 9 bytes for `wTotalLength`,
then a full read of the contiguous configuration tree. The walker
records every Interface descriptor and the first interrupt-IN
Endpoint under each into `c->slots[].interfaces[]`.

**UC.3 shipped.** `Croi_Xhci_SetConfiguration` issues a no-data
control transfer (bmRequestType=0x00, bRequest=9) to drive the
device into the Configured state. Both `usb-kbd` and `usb-mouse`
report `usb_configured = true` post-boot. Note: the xHCI Slot
State stays at Addressed (2) until UC.5's Configure Endpoint
Command runs; USB-configured and xHCI-configured are tracked as
distinct states.

**UC.4 shipped.** `Croi_Xhci_DispatchInterfaces` classifies every
parsed interface into `XhciInterfaceDispatch`
(NONE/KEYBOARD/MOUSE/HID-other/UNSUPPORTED). HID/Boot/Keyboard and
HID/Boot/Mouse are tagged as eligible for the (Tier 3) HID Gleas;
non-HID classes log "unsupported in Phase 1".

**UC.5 shipped.** `Croi_Xhci_ConfigureHidInterrupts` walks every
HID-dispatched interface, allocates a per-endpoint interrupt-IN
Transfer Ring (with a Link TRB at the tail), builds an Input
Context, and issues the Configure Endpoint Command. Both QEMU
devices transition Slot State Addressed (2) → Configured (3).

**HA.1 shipped (kernel-side).** `Croi_Xhci_HidSetProtocol` issues
the USB HID 1.11 §7.2.6 SET_PROTOCOL class request (bmRequestType=
0x21, bRequest=0x0B, wValue=0=Boot, wIndex=interface number,
wLength=0). `Croi_Xhci_HidSetBootProtocols` runs it across every
HID/Boot dispatched interface. Both `usb-kbd` and `usb-mouse`
report `boot-protocol selected` post-boot. The devices are now
pinned to the canonical 8-byte boot-protocol report layout —
keyboards emit 8-bit modifiers + reserved + 6-key rollover, mice
emit 8-bit buttons + 8-bit X delta + 8-bit Y delta + 8-bit wheel.
No HID Report Descriptor parser needed for Phase 1.

Note on layering: the doc places HA.1 inside Tier 3 (HID Gleas
territory), but the SET_PROTOCOL call is just a control transfer
on EP0 and lands cleanly in the kernel-side substrate. When the
HID Gleas eventually moves to U-mode (post HB.1 / Phase 3 V36+
device subset), it can re-issue SET_PROTOCOL via the `usb.device`
LVO surface — but the device staying in Boot mode is idempotent,
so the kernel-side bring-up costs nothing.

UB.6/UB.7 substrate (TRB ring helpers, polling event-ring consumer)
landed alongside UB.5. Interrupt-driven event handling is still
deferred — Phase 1 polls.

Still to ship before Tier 2 exits:
- UC.6 — `usb_enum_smoke` extension covering the configuration tree.
  `xhci_smoke` already asserts the descriptor tree, dispatch,
  Configure Endpoint, and SET_PROTOCOL invariants for QEMU
  usb-kbd / usb-mouse; UC.6 is mostly test-organisation.

Still deferred from Tier 1:
- UB.7 done-for-real: interrupter wired up so we don't busy-poll.

Tier 3 still owed (mostly waits on Phase 3 / Subgoal 6):
- HA.2 — Mouse boot-protocol decoder (8-bit buttons / X / Y / wheel)
- HA.3 — Keyboard boot-protocol decoder (modifiers + 6-key rollover)
- HA.4 — USB usage-code → V36+ rawkey table
- HB.1 — `usb.device` LVO surface (Phase 3 V36+ device subset)
- HB.2 — `hid` Gleas as a U-mode program
- HB.3 — Croi spawns the `hid` Gleas at boot
- HB.4 — End-to-end QEMU `sendkey` smoke test

---

## Context

`docs/ROADMAP.md` Phase 1 names two USB requirements:

- **Real-hardware path.** `xHCI driver on the X1's USB controller.
  HID class driver in user space (a Gleas) that posts events to
  Leargas.`
- **QEMU equivalent.** The same Croi binary boots under
  `qemu-system-riscv64 -machine virt` and runs through the same
  xHCI driver against QEMU's `qemu-xhci` device — a real xHCI 1.0
  implementation — with `usb-kbd` / `usb-mouse` devices attached.

**No virtio shortcut.** CaraOS is a real-hardware OS; QEMU is the
daily driver, but the daily driver runs the same code paths the
silicon does. virtio-input would have been faster to bring up but
it's a parallel dead end — every virtio-only line of code would
have to be deleted before RV2 silicon shipped, and the QEMU/silicon
divergence would mean QEMU green doesn't imply silicon green. The
cleanroom xHCI driver is one path that works in both places.

### Strategy

**One xHCI driver, two transports underneath.** xHCI 1.0+ is a
spec'd PCI/PCIe device (USB 3.x Implementer's Forum, eXtensible
Host Controller Interface for Universal Serial Bus, current is
1.2b). The driver speaks xHCI; what it sits on is one of:

- QEMU `qemu-xhci` (PCIe device, `-machine virt -device qemu-xhci
  -device usb-kbd -device usb-mouse`). Fully xHCI 1.0 compliant.
- The X1's onboard xHCI controller. PCIe-discoverable; the X1
  TRM names the controller and its base class is
  `0x0C / 0x03 / 0x30` (USB host) per PCI conventions.

The two paths share PCIe enumeration, register layout, command
ring, transfer ring, and interrupt model. They diverge only on
peripheral details (interrupt routing, integrated PHY config),
which the per-platform PCI config-space match handles.

**Three Tiers** layered for clean exit criteria:

1. **Tier 1 — xHCI controller online.** PCIe enumeration finds
   the controller; xHCI init runs through to `Run/Stop = 1`; an
   attached HID device's port shows `Connected`; the device's
   slot transitions Default → Address → Configured.
2. **Tier 2 — USB device enumeration.** Standard control
   transfers (Get Descriptor, Set Configuration) complete against
   the connected device; device / configuration / interface /
   HID descriptors readable.
3. **Tier 3 — HID class driver as a Gleas.** A user-mode HID
   Gleas reads the interrupt-IN endpoint, decodes the boot-
   protocol mouse / keyboard reports, and posts
   `IECLASS_RAWMOUSE` / `IECLASS_RAWKEY` events into Leargas's
   input port.

### Out of scope for Phase 1

- **virtio-input.** Explicitly not pursued — no schedule shortcut
  via emulated virtio devices. The real-hardware xHCI path is
  what ships; QEMU runs the same driver against `qemu-xhci`.
- **USB hub support.** Devices plug straight into the host's
  root ports for Phase 1; cascaded hubs arrive with Phase 5 SBC
  peripheral coverage.
- **USB mass storage / printer / audio classes.** Phase 5.
- **Power management** (selective suspend, runtime PM). Stretch.
- **USB-C alternate modes / DisplayPort over USB.** Phase 5+.
- **Wireless input (Bluetooth HID).** No Bluetooth stack in v0.
  The user must use wired peripherals.
- **xHCI debug capability** as a kernel diagnostic interface.
  Diagnostic surface stays NS16550 + framebuffer logsinks.
- **HID Report Descriptor parser.** Phase 1 forces boot protocol
  via `SET_PROTOCOL(Boot)` so the report layout is fixed
  (8 bytes mouse / 8 bytes keyboard); the full descriptor parser
  is Phase 5.

---

## Tier 1 — xHCI controller online

**Exit:** the kernel boots, discovers the xHCI controller via
PCIe, brings it through reset, completes xHCI init (DCBAA,
Command Ring, Event Ring, Run/Stop), and a connected HID device's
port reports `Connected` with the slot enabled. An in-kernel
`xhci_smoke` test asserts these state transitions on both QEMU
`qemu-xhci` and (when available) the X1.

### Epic UA — PCIe enumeration

**Goal:** walk the platform's PCIe root complex, identify each
function by class code, hand each one off to its driver.
PCIe 5.0 spec is the source.

- **UA.1** FDT discovery of the PCIe root complex node. Parse
  `reg`, `ranges` (for the BAR allocation window), `interrupt-map`
  (for legacy IRQ routing), MSI/MSI-X capability discovery. The
  X1 and QEMU virt both expose a PCIe host bridge in the FDT;
  the parser handles both.
- **UA.2** Configuration-space access primitive (ECAM-shaped on
  both targets). `Croi_Pci_Read{8,16,32}` /
  `Croi_Pci_Write{8,16,32}` on (bus, device, function, offset).
- **UA.3** Bus walk: scan bus 0 device functions, recurse into
  bridges. Per-function: read VID/DID/class, allocate BARs from
  the `ranges` window, write back, store a `struct PciFunction`
  per matched device.
- **UA.4** Match xHCI by class code (`0x0C / 0x03 / 0x30`) and
  hand off to the xHCI driver below.
- **UA.5** Cleanroom note: PCIe init is implemented from the
  PCI-SIG specs (PCI Express Base Spec 5.0+, PCI Local Bus 3.0
  for legacy config space). No `libpci`, no Linux kernel snippets.

### Epic UB — xHCI host controller driver

**Goal:** xHCI 1.2 controller initialised; Event Ring servicing
interrupts; one Slot allocated; one Endpoint Context configured.
The xHCI 1.2b spec (eXtensible Host Controller Interface for
Universal Serial Bus, USB-IF) is the source.

- **UB.1** Capability registers parse (`HCSPARAMS1`,
  `HCSPARAMS2`, `HCCPARAMS1`, page size, max slots, max ports).
- **UB.2** Operational registers: reset (USBCMD HCRST), wait
  CNR clear, set MaxSlotsEn, allocate DCBAA + scratchpad
  buffers from the page allocator, program DCBAAP.
- **UB.3** Command Ring + Event Ring allocation, ERSTBA
  programming, `Run/Stop = 1`.
- **UB.4** Port reset and Slot allocation: walk PORTSC for
  each USB2/USB3 port that reports Connected, send
  `Enable Slot` command, wait completion event.
- **UB.5** Address Device + Configure Endpoint TRBs: build the
  Input Context, send the command, parse the response.
- **UB.6** TRB ring management: enqueue / dequeue helpers,
  Cycle bit handling, ring wrap, doorbell write.
- **UB.7** Event ring interrupt handler: read ERDP, walk the
  ring, dispatch by TRB type (transfer / command-completion /
  port-status-change).
- **UB.8** `xhci_smoke` kernel test: spawns the driver against
  the discovered controller, asserts state transitions through
  reset → init → first device addressed. Runs under QEMU
  `qemu-xhci` (the daily driver) and on the X1 once the
  hardware path lands.

---

## Tier 2 — USB device enumeration

**Exit:** standard control-pipe transfers complete; device,
configuration, interface, and HID descriptors readable; an HID
interface is recognised and routed to the (yet-to-be-spawned)
HID Gleas.

### Epic UC — Standard descriptor reads

**Goal:** the canonical USB enumeration sequence runs against any
attached HID device. USB 2.0 spec §9 is the source (Standard
Device Requests).

- **UC.1** `GET_DESCRIPTOR(Device)`: full 18-byte device
  descriptor read. Identifies VID/PID/protocol class; hands
  protocol back to the dispatcher.
- **UC.2** `GET_DESCRIPTOR(Configuration)`: short read for
  total length, then full read for the configuration + nested
  interfaces + endpoints + HID descriptor.
- **UC.3** `SET_CONFIGURATION` to activate the chosen
  configuration. Phase 1 always picks configuration index 0.
- **UC.4** Interface dispatch: HID interfaces (class = 0x03)
  routed to the HID class driver (Tier 3); mass-storage / audio
  / etc. logged as "unsupported in Phase 1" and ignored.
- **UC.5** Endpoint Context configuration for the HID interface's
  interrupt-IN endpoint via the xHCI Configure Endpoint command.
- **UC.6** A kernel test (`usb_enum_smoke`) attaches a `usb-kbd`
  device under QEMU and asserts the descriptor reads return the
  canonical HID-keyboard interface descriptor (class 0x03,
  subclass 0x01 boot, protocol 0x01 keyboard).

---

## Tier 3 — HID class driver as a Gleas

**Exit:** a USB keyboard and a USB mouse plugged into the host
generate events that flow through the HID Gleas into Leargas's
input port. A boot-time integration test plugs in synthetic
devices and asserts the event stream.

### Epic HA — HID class driver

**Goal:** the boot-protocol HID parser runs as a Gleas Croi
spawns at boot. It opens the relevant USB endpoint (interrupt-IN),
reads 8-byte boot-protocol report packets, decodes them into
`IECLASS_RAWMOUSE` / `IECLASS_RAWKEY` events, and `PutMsg`s into
Leargas's input port. USB HID 1.11 spec (§B for boot protocols).

- **HA.1** HID boot-protocol selection: `SET_PROTOCOL(Boot)` on
  the interface so we get the canonical 8-byte mouse / 8-byte
  keyboard report formats and don't need a HID Report Descriptor
  parser yet.
- **HA.2** Mouse boot-protocol decoder: 8 bits buttons, 8-bit X
  delta, 8-bit Y delta, 8-bit wheel. Map button bits to V36+
  IEQUALIFIER bits, X/Y deltas to `ie_X` / `ie_Y`, generate
  `IECMP_MOUSEBUTTONS` / `IECMP_MOUSEMOVE` events.
- **HA.3** Keyboard boot-protocol decoder: 8 bits modifiers,
  reserved, 6-key rollover. Map USB usage codes to V36+ rawkey
  codes (the V36+ rawkey table is in `<devices/keymap.h>`).
- **HA.4** USB-to-V36+ usage-code translation table in
  `src/userland/hid/usbkeymap.c`.

### Epic HB — Gleas wiring

**Goal:** the HID driver runs as a U-mode task spawned by Croi
at boot, opens the USB endpoint via `OpenDevice("usb.device", …)`
or its CaraOS equivalent (TBD; see ARCHITECTURE.md §14.7), and
delivers events to Leargas.

- **HB.1** `usb.device` LVO surface — `OpenDevice` /
  `CloseDevice` / `BeginIO` / `AbortIO` plus a small set of USB-
  specific commands (`USBCMD_INTERRUPT_IN`,
  `USBCMD_CONTROL_IO`). Goes through the lvo-gen pipeline like
  every other CaraOS device. Phase 3's V36+ device subset
  delivers the trampolines.
- **HB.2** A `hid` Gleas: tiny U-mode program that enumerates
  HID devices via `usb.device`, spawns a per-device read loop,
  blocks on interrupt-IN reads, and `PutMsg`s
  `struct InputEvent` records to Leargas's port.
- **HB.3** Croi spawns the `hid` Gleas at boot once the xHCI
  controller is up and there's at least one HID device
  enumerated. (Hot-plug deferred to Phase 5.)
- **HB.4** End-to-end test: the QEMU smoke harness types the
  string "abc" via `sendkey`, asserts Leargas's input ring sees
  three IECLASS_RAWKEY events with the right rawkey codes. Same
  test runs on the X1 via the `qemu-system-riscv64 -monitor`
  control surface (or, on real silicon, an attached USB
  keyboard during a manual test).

---

## What this unblocks

- **Phase 1 Subgoal 6 (Leargas).** The pointer position tracker
  and focused-window keyboard router both consume events from
  the input ring this stack produces.
- **Phase 1 Subgoal 7 (Clar).** Clicking a drawer to open it,
  typing into a text Inntin, dragging a window — all driven by
  the events flowing through here.
- **Phase 5 SBC peripheral coverage.** Beyond mouse/keyboard:
  USB hubs, gamepads, USB-C audio, USB-Ethernet — each one
  registers a class driver against the Tier 1 xHCI substrate.
  No Phase 1 code needs to change.
