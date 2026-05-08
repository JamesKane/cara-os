# Phase 1 Subgoal 5 — USB host stack

> Plan for the USB stack that delivers mouse and keyboard events
> to Leargas (Subgoal 6) and ultimately into Clar (Subgoal 7). Pairs
> with `docs/ROADMAP.md` Phase 1 Subgoals 5–7,
> `docs/PHASE1_RUNTIME.md` (the Croi runtime substrate), and
> `docs/HARDWARE_RV2.md` (where on the X1 the USB controller sits).

---

## Status — 2026-05-08

**Phase 1 Subgoal 5 kernel-side substrate is end-to-end shipped.**
Every layer from PCIe enumeration to a decoded boot-protocol HID
report runs to completion under QEMU's `qemu-xhci` against
`-device usb-kbd -device usb-mouse`. What's left for the subgoal
is gated on Phase 3 (the V36+ `usb.device` LVO surface) which the
HID Gleas (HB.*) will sit on top of.

### Pipeline at HEAD (`9f25d6f`)

```
PCIe enum → xHCI HCRST → DCBAA + Cmd/Event rings → Run/Stop
  → PORTSC walk → Port Reset → Enable Slot → Address Device
  → GET_DESCRIPTOR(Device) → GET_DESCRIPTOR(Configuration)
  → SET_CONFIGURATION → DispatchInterfaces (HID/Boot tagged)
  → Configure Endpoint Command → SET_PROTOCOL(Boot)
  → HidIntReadOnce → CaraHidReport (decoded)
```

Each step is its own commit (`phase-1/UB.4` → `phase-1: int-IN
read`); see `git log --oneline` for the exact chain.

### How to verify on resume

```bash
# rv64 build
cd build-rv64 && make -j

# host unit tests + smoke (12/12 passing as of HEAD)
cd ../build-host && make -j && ctest

# manual end-to-end with key injection
QEMU=$(command -v qemu-system-riscv64)
MONSOCK=/tmp/cara-mon.sock; rm -f $MONSOCK
${QEMU} -M virt -m 256 -nographic -bios default \
    -kernel build-rv64/src/croi/croi.elf \
    -device qemu-xhci -device usb-kbd -device usb-mouse \
    -monitor unix:$MONSOCK,server=on,nowait > /tmp/cara.log 2>&1 &
sleep 0.04 && echo 'sendkey a' | nc -U $MONSOCK -w 1 > /dev/null
sleep 0.5 && kill %1 2>/dev/null
grep -E 'int-IN|kbd:|mouse:' /tmp/cara.log
```

Expected: `slot=1 iface[0] kbd int-IN report (8 bytes)` followed
by a decoded record. Mouse will say `idle (no input)`.

### Resume here — pick one

The substrate is solid; the next moves are independent. Pick by
strategic preference:

1. **Phase 1 Subgoal 6 — Leargas (input ring + window-event
   router).** `docs/PHASE1_LEARGAS.md` plans it. Doesn't need the
   HID Gleas in place because Leargas owns the event-ring
   contract; the Gleas wires to it later. This unblocks Subgoal 7
   (Clar) and lets Subgoal 5 fully complete via HB.* on top.

2. **Tighten the smoke harness.** Extend
   `tests/boot/smoke_qemu_kernel.sh` to drive QEMU's `-monitor`
   socket and assert the int-IN substrate captures an injected
   keystroke. Right now end-to-end verification is manual. Small
   commit (~30 lines of shell), high CI value.

3. **UB.7 done-for-real.** Wire the xHCI interrupter (real MSI/X
   or platform IRQ) so events post via interrupts instead of
   polling. Polling works fine for Phase 1 but real silicon
   wants this. Bigger lift; needs IRQ-controller glue from
   `croi/sched`.

4. **HB.* — wait for Phase 3.** The `usb.device` LVO surface
   (HB.1) is part of the V36+ device subset that lands in
   Phase 3. Until that exists, the HID Gleas can't ride on it
   and the substrate stays at the kernel-side primitives we have.

### Tier-by-tier status

| Tier | Epic                                              | Status |
|------|---------------------------------------------------|--------|
| 1    | UA — PCIe enumeration                             | ✅     |
| 1    | UB.1–5 — xHCI controller, DCBAA, rings, Address Device | ✅ |
| 1    | UB.6/7 substrate (TRB helpers + polling event ring)   | ✅ (interrupts deferred) |
| 2    | UC.1 — GET_DESCRIPTOR(Device)                     | ✅     |
| 2    | UC.2 — GET_DESCRIPTOR(Config) + parse             | ✅     |
| 2    | UC.3 — SET_CONFIGURATION                          | ✅     |
| 2    | UC.4 — Interface dispatch                         | ✅     |
| 2    | UC.5 — Configure Endpoint Command                 | ✅     |
| 2    | UC.6 — formal `usb_enum_smoke`                    | folded into `xhci_smoke` |
| 3    | HA.1 — SET_PROTOCOL(Boot)                         | ✅ kernel-side |
| 3    | HA.2 — Mouse boot decoder                         | ✅     |
| 3    | HA.3 — Keyboard boot decoder                      | ✅     |
| 3    | HA.4 — USB → V36+ rawkey table                    | ✅     |
| 3    | Interrupt-IN read primitive (`HidIntReadOnce`)    | ✅     |
| 3    | HB.* — `usb.device` LVO + `hid` Gleas             | blocked on Phase 3 |
| —    | UB.7 done-for-real (interrupter)                  | deferred |
| —    | Smoke harness drives `-monitor` socket            | deferred |

### Gotchas this session paid for — leave them documented

1. **QEMU's `qemu-xhci` latches `CRCR_LO` on the `CRCR_HI` write.**
   The canonical xHCI bring-up order is HI-then-LO; QEMU's xHC runs
   `xhci_ring_init()` on the HI write using the *currently-latched*
   LO value. HI-then-LO points the controller's command-ring
   dequeue at zero and *every* doorbell ring silently no-ops. Fix
   is permanent in `src/croi/xhci/setup.c:130-140` (LO first, with
   RCS, then HI). Real silicon also accepts LO-first, so this is
   the universal-correct order. ~2 hours debugging cost.

2. **QEMU's `usb-kbd` coalesces press+release reports.** A bare
   `sendkey a` issues both press and release through the HID
   device queue inside ~10ms; QEMU's device-side queue overwrites
   pending reports with "latest state", so when the kernel int-IN
   poll consumes the next report, it sees "all keys up". To
   capture the press, either drive `sendkey` with `-hold-time` or
   move to a continuous-poll loop (which is what the eventual HID
   Gleas does anyway). Not a substrate bug; expected behaviour.

3. **Kernel logger only supports `%x`, no width specifiers.**
   `%04x` parses as default → emits `?4x` literal AND skips a
   va_arg, shifting all subsequent format args. `src/croi/log/log.c`
   has the canonical formatter; if you need width-padding, extend
   the formatter rather than expecting `%04x` to work.

4. **`/Makefile`, `/gen/`, `/src/**/Makefile` are now in
   `.gitignore`.** A prior in-source CMake configure left
   generated Makefiles in the working tree; one `git add -A` in
   this session swept them up and required a `reset --soft` to
   undo. Use explicit file lists with `git add` until you're sure
   no other in-source build artifacts are lurking.

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
