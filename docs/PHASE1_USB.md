# Phase 1 Subgoal 5 — USB host stack

> Plan for the USB stack that delivers mouse and keyboard events
> to Leargas (Subgoal 6) and ultimately into Clar (Subgoal 7). Pairs
> with `docs/ROADMAP.md` Phase 1 Subgoals 5–7,
> `docs/PHASE1_RUNTIME.md` (the Croi runtime substrate), and
> `docs/HARDWARE_RV2.md` (where on the X1 the USB controller sits).

---

## Status — 2026-05-08

**Not started.** The Phase 1 substrate (Tier 1+2+3 of
PHASE1_RUNTIME) is in place; the framebuffer (Subgoal 4) is
shipped; nothing actually delivers pointer or keyboard events yet.

---

## Context

`docs/ROADMAP.md` Phase 1 names two USB requirements that look like
one but aren't:

- **Real-hardware path.** `xHCI driver on the X1's USB controller.
  HID class driver in user space (a Gleas) that posts events to
  Leargas.`
- **QEMU equivalent.** `the same Croi binary boots under
  qemu-system-riscv64 -machine virt and shows Clar in the QEMU
  display output, with virtio-keyboard and virtio-mouse generating
  events. Both must work; QEMU is the daily driver.`

These are two transports for the same input pipeline. virtio-input
is qualitatively easier (no PCIe enumeration, no controller driver,
no USB protocol — virtio devices ride MMIO with descriptor rings)
and gives us a working mouse and keyboard for Leargas/Clar
development *before* xHCI is up. xHCI is the stricter requirement
because real RV2 silicon ships with USB ports, not virtio devices.

The strategy below ships virtio-input first so Leargas and Clar
have something to consume; xHCI lands once Leargas/Clar work
qualitatively, removing schedule dependency between the input
pipeline and the GUI work.

### Strategy

**Both transports feed the same `input.device`-shaped surface.**
The input.device LVOs are V36+ canonical (`AddIO` /
`CheckIO` / `RemIO` etc., already covered by the Phase 3
`exec.library` subset), and Leargas reads events through the
public `IECLASS_RAWMOUSE` / `IECLASS_RAWKEY` enum from
`<devices/inputevent.h>`. Whichever transport produces the event
(virtio descriptor or USB HID interrupt-pipe transfer), the queue
into Leargas looks identical.

The work splits into three Tiers:

1. **Tier 1 — virtio-input under QEMU.** Simplest delivery path.
   Unblocks Leargas and Clar without waiting on the controller
   driver.
2. **Tier 2 — xHCI controller driver + USB enumeration on real
   hardware.** Brings up the X1's USB controller, walks the device
   tree, parses descriptors.
3. **Tier 3 — HID class driver as a Gleas.** Polls the
   interrupt-IN pipe from the keyboard / mouse, reformats reports
   into `IECLASS_RAWMOUSE` / `IECLASS_RAWKEY` events, and
   `PutMsg`s them into Leargas's input port.

### Out of scope for Phase 1

- **USB hub support.** Devices plug straight into the host's root
  ports for Phase 1; cascaded hubs arrive with Phase 5 SBC
  peripheral coverage.
- **USB mass storage / printer / audio classes.** Phase 5.
- **Power management** (selective suspend, runtime PM). Stretch.
- **USB-C alternate modes / DisplayPort over USB.** Phase 5+.
- **Wireless input (Bluetooth HID).** No Bluetooth stack in v0.
  The user must use wired peripherals.
- **xHCI debug capability** as a kernel diagnostic interface.
  Diagnostic surface stays NS16550 + framebuffer logsinks.

---

## Tier 1 — virtio-input under QEMU

**Exit:** `qemu-system-riscv64 -machine virt -device virtio-keyboard
-device virtio-mouse` brings up two virtio-input devices; pressing
a key or moving the mouse posts an event on a kernel-side input
ring; an in-kernel test asserts it received N events of each type
in a scripted QEMU `sendkey` / mouse-move sequence.

### Epic VA — virtio-mmio driver

**Goal:** discover virtio devices listed in the FDT at
`/soc/virtio_mmio@*`, classify by `device-id`, hand each off to its
class driver. virtio-MMIO is fully spec'd (virtio v1.2 §4.2);
no firmware blob.

- **VA.1** FDT walk for `virtio_mmio` nodes. Parse `reg` for the
  4 KiB MMIO region. Probe `MagicValue` / `Version` /
  `DeviceID` registers. Reject `DeviceID == 0` (slot empty).
- **VA.2** Per-device init: feature negotiation, virtqueue
  allocation (using `Croi_Alloc` for descriptor / available /
  used ring storage in upper-half kernel pages, page-aligned),
  `QueueReady`, `DRIVER_OK`.
- **VA.3** Common interrupt handler stub. Reads
  `InterruptStatus`, dispatches to the per-device queue handler.

### Epic VB — virtio-input class driver

**Goal:** consume input events from a `virtio-input` device's
`eventq` and post to a kernel-side input ring. The
`virtio-input` device class is spec'd in virtio v1.2 §5.8.

- **VB.1** Initial config space read — pull the device's name,
  serial, properties tag (which axes / button counts / key
  ranges) into a per-device descriptor.
- **VB.2** `eventq` consumer: pull `virtio_input_event` records
  (`type`, `code`, `value`) off the used ring, wrap in a CaraOS
  `IECLASS_RAW{MOUSE,KEY}`-shaped event, append to the input ring.
- **VB.3** Replenish the eventq descriptors after consume (to
  keep the ring full).

### Epic VC — kernel-side input ring + test

**Goal:** events flow from virtio-input through the input ring to
a test consumer that asserts ordering and totals.

- **VC.1** `struct InputEvent` — V36+ shape from
  `<devices/inputevent.h>` with `ie_Class` / `ie_Code` /
  `ie_Qualifier` / `ie_X` / `ie_Y`.
- **VC.2** `KOBJ_INPUT_RING`: a typed Ring (per `cara/ring.h`)
  carrying InputEvent slots. Multiple producers (one per
  virtio-input device); single consumer (eventually Leargas, in
  Phase 1 Subgoal 6).
- **VC.3** `T_virtio_input_smoke`: spawn a kernel test task that
  consumes from the input ring; QEMU script (test harness) sends
  `sendkey a` and `mouse_move`; assert N keypress + M motion
  events arrive in order.

---

## Tier 2 — PCIe enumeration + xHCI controller (real hardware)

**Exit:** the kernel boots on RV2 silicon, discovers the X1's USB
controller via PCIe, brings it out of reset, completes xHCI
controller initialisation (CRCR / DCBAA / Event Ring), and an
attached HID device's slot transitions through Default → Address →
Configured states with descriptor reads succeeding.

### Epic UA — PCIe enumeration on X1

**Goal:** walk the X1's PCIe root complex, identify each function
by class code, hand each one off to its driver.

- **UA.1** FDT discovery of the PCIe root complex node. Parse
  `reg`, `ranges` (for the BAR allocation window), `interrupt-map`
  (for legacy IRQ routing), MSI/MSI-X capability discovery.
- **UA.2** Configuration-space access primitive (ECAM-shaped
  on the X1; check the SoC TRM). `Croi_Pci_Read{8,16,32}` /
  `Croi_Pci_Write{8,16,32}` on (bus, device, function, offset).
- **UA.3** Bus-walk: scan bus 0 device functions, recurse into
  bridges. Per-function: read VID/DID/class, allocate BARs from
  the `ranges` window, write back, store a
  `struct PciFunction` per matched device.
- **UA.4** Match xHCI by class code (0x0C / 0x03 / 0x30) and hand
  off to the xHCI driver below.

### Epic UB — xHCI host controller driver

**Goal:** xHCI 1.2 controller initialised; Event Ring servicing
interrupts; one Slot allocated; one Endpoint Context configured.
xHCI 1.2 spec is the source.

- **UB.1** Capability registers parse (`HCSPARAMS1`,
  `HCSPARAMS2`, `HCCPARAMS1`, page size, max slots, max ports).
- **UB.2** Operational registers: reset (USBSTS HCH / USBCMD
  HCRST), wait CNR, set MaxSlotsEn, allocate DCBAA + scratchpad
  buffers from the page allocator.
- **UB.3** Command Ring + Event Ring allocation, ERSTBA / DCBAAP
  programming, Run/Stop.
- **UB.4** Port reset and Slot allocation: enable each USB2/USB3
  port that reports Connected, USBSTS PORTSC bit walk, send
  `Enable Slot` command, wait completion event.
- **UB.5** Address Device + Configure Endpoint TRBs: build the
  Input Context, send the command, parse the response.
- **UB.6** TRB ring management: enqueue / dequeue helpers,
  Cycle bit handling, ring wrap, doorbell write.
- **UB.7** Event ring interrupt handler: read ERDP, walk the
  ring, dispatch by TRB type (transfer / command-completion /
  port-status-change).

### Epic UC — USB device enumeration

**Goal:** standard control-pipe transfers complete; device,
configuration, interface, and HID descriptors readable. USB 2.0
spec §9 is the source.

- **UC.1** `GET_DESCRIPTOR(Device)`: full 18-byte device
  descriptor read, identifies VID/PID/protocol.
- **UC.2** `GET_DESCRIPTOR(Configuration)`: short read for
  total length, then full read for the configuration + nested
  interfaces + endpoints + HID descriptor.
- **UC.3** `SET_CONFIGURATION` to activate the chosen config.
- **UC.4** Interface dispatch: HID interfaces (class = 0x03)
  routed to the HID class driver below; mass-storage / audio /
  etc. logged as "unsupported in Phase 1" and ignored.

---

## Tier 3 — HID class driver as a Gleas

**Exit:** a USB keyboard and a USB mouse plugged into the RV2 (or
their virtio counterparts under QEMU) generate events that flow
through the HID Gleas into Leargas's input port. A boot-time
integration test plugs in synthetic devices and asserts the event
stream.

### Epic HA — HID class driver

**Goal:** the boot-protocol HID parser runs as a Gleas Croi spawns
at boot. It opens the relevant USB endpoint (interrupt-IN), reads
8-byte boot-protocol report packets, decodes them into
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
- **HA.4** USB-to-V36+ usage-code translation table (lives in
  `src/userland/hid/usbkeymap.c`). Same table covers virtio-input
  Linux keycodes (Tier 1 Epic VB) since they're isomorphic to
  USB usage codes for the boot subset.

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
  three IECLASS_RAWKEY events with the right rawkey codes.

---

## What this unblocks

- **Phase 1 Subgoal 6 (Leargas).** The pointer position tracker
  and focused-window keyboard router both consume events from the
  input ring this stack produces.
- **Phase 1 Subgoal 7 (Clar).** Clicking a drawer to open it,
  typing into a text Inntin, dragging a window — all driven by
  the events flowing through here.
- **Phase 5 SBC peripheral coverage.** Beyond mouse/keyboard:
  USB hubs, gamepads, USB-C audio, USB-Ethernet — each one
  registers a class driver against the Tier 2 xHCI substrate.
  No Phase 1 code needs to change.
