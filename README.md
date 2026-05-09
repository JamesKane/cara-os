# CaraOS // SILICON REBELLION

**A cleanroom C23/RISC-V resurrection of the AmigaOS Release 2 soul.**

The modern OS is a cage. It’s a multi-tenant corporate panopticon designed to protect the hardware from the user and the user from themselves. 

**CaraOS is the escape hatch.**

We’re stripping away the layers of "security" that serve only to lock you out. We’re returning to a time when a pointer was a promise, the CPU was your servant, and the machine was an extension of your intent. No UIDs. No ACLs. No telemetry. No corporate black boxes.

## ⚡ THE DISCLAIMER (READ OR REAP)

**CaraOS is engineered for the raw complexity of the late-80s and early-90s.**

Modern safety nets have been cut. We have abandoned the illusions of multi-tenant isolation and cryptographically enforced gatekeeping.

- **The User is God.** You have full control over every register, every page table, and every peripheral.
- **Fault Containment, Not Security.** The MMU exists to keep a crashing tool from blowing out the kernel, not to hide secrets from you.
- **Zero Trust? No. Total Trust.** IPC is direct pointer passing. If a task has a pointer, it has the data. 

**DO NOT DO YOUR BANKING HERE.** And while you're at it:

- **No password vaults, SSH keys, wallet seeds, or API tokens.** Any task that gets a pointer reads them. There is no keychain, no secure enclave, no per-process memory you can hide in. Plaintext is plaintext.
- **No regulated data — yours or anyone else's.** No audit log, no access control, no compliance story. HIPAA, GDPR, PCI-DSS, SOX, FedRAMP — none of them recognise this OS exists, and they shouldn't.
- **No public-facing services.** No firewall, no privilege separation, no patch pipeline, no CVE process. Putting a Croi machine on the open internet is a dare, not a deployment.
- **No untrusted binaries.** A Gleas you don't trust is a Gleas that owns the machine — there is no sandbox to contain it, and "user space" is a polite fiction about register state, not isolation.
- **No shared tenancy.** There is exactly one user: you. If two of you sit down, there is still one of you, and you will fight over the address space.

This system is for hackers, retro-engineers, and those who believe that the street finds its own uses for things. If you connect this to a hostile network, you're on your own.

## THE PILLARS

- **Cleanroom Extraction:** We build from the 3rd Edition RKMs—the sacred texts. No proprietary code, no stolen artifacts. We manifest the spec from first principles.
- **Dependency Nihilism:** We vendor nothing. No `libfdt`, no bloated stacks, no corporate "reference implementations." If it runs in the CaraOS image, we wrote it from the silicon up.
- **The C23 Razor:** Legacy macros are dead. We use `nullptr`, `constexpr`, `typeof`, and atomic primitives to carve a lean, modern path through the old ways.
- **RISC-V Native:** We target the open silicon—primarily the **Spacemit X1 / OrangePi RV2**. We develop in the shadows of QEMU, but we live on the metal.

## ARCHITECTURE

CaraOS is a single-address-space OS for RISC-V 64. Splanc (a UEFI app) hands the FDT and memory map to Croi (the kernel) in S-mode; every Gleas — drivers, libraries, Clar itself — runs in U-mode but shares one Sv39 address space. The MMU exists for fault containment, not privilege. Kernel objects are reached through opaque Handles; libraries jump through Library Vector Offsets, the AmigaOS V36+ shape. IPC is MsgPort over a lock-free Ring with no copy. The brand namespace (Croi/Dath/Leargas/Logaic/Clar) is the implementation; classic names (`exec.library`, `intuition.library`) trampoline in at Phase 3.

## SYSTEM ANATOMY

- **Croi** (The Heart): The Exec kernel. Multitasking without the bloat.
- **Logaic** (The Logic): The DOS. Filesystems that make sense, not hierarchies of bureaucracy.
- **Leargas** (The Vision): The Intuition. Direct management of the visual matrix.
- **Clar** (The Canvas): The Workbench. A desktop for those who still know how to use one.
- **Splanc** (The Spark): The UEFI spark that ignites the machine.

## STATUS: PHASE 1 // BOOT TO CLAR

Phase 0 (the foundation) is done. Phase 1 ships when `splanc.efi` boots
on the OrangePi RV2, Croi reaches multitasking, and a USB mouse +
keyboard drive a pointer and type into Clar. We are mid-flight:

- [x] `splanc.efi` boots under QEMU `virt`; Croi takes over in S-mode.
- [x] Croi runtime: Sv39 paging, frame allocator, kernel heap,
      multi-hart scheduler, signals, MsgPort/Ring IPC, Handle table,
      syscall trampoline.
- [x] UART0 console via FDT discovery; SBI early console before that.
- [x] Dath framebuffer (CPU-only) — boot banner + structured log sink.
- [x] xHCI host: PCIe enum → HCRST → rings → port reset → Address
      Device → descriptors → SET_CONFIG → HID boot-protocol interrupt-IN
      reads decoding to `CaraHidReport`.
- [ ] Leargas (Intuition) — pointer + focused-window event delivery.
- [ ] Clar (Workbench) — background screen, drawer, gadget input.
- [ ] Bring-up on real OrangePi RV2 silicon.

See `docs/ROADMAP.md` for the nine-phase deployment sequence and the
`docs/PHASE1_*.md` files for per-subgoal status.

## FORGING THE IMAGE

You need the RISC-V cross-compiler (Clang) and CMake. 

```bash
mkdir build-rv64 && cd build-rv64
cmake -DCMAKE_TOOLCHAIN_FILE=../cmake/toolchain-riscv64.cmake ..
make
```

## THE COMPACT

CaraOS is licensed under the **BSD 2-Clause License**. See `LICENSE`. Every file carries the SPDX mark. No corporate strings attached.
