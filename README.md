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

**DO NOT DO YOUR BANKING HERE.** This system is for hackers, retro-engineers, and those who believe that the street finds its own uses for things. If you connect this to a hostile network, you’re on your own.

## THE PILLARS

- **Cleanroom Extraction:** We build from the 3rd Edition RKMs—the sacred texts. No proprietary code, no stolen artifacts. We manifest the spec from first principles.
- **Dependency Nihilism:** We vendor nothing. No `libfdt`, no bloated stacks, no corporate "reference implementations." If it runs in the CaraOS image, we wrote it from the silicon up.
- **The C23 Razor:** Legacy macros are dead. We use `nullptr`, `constexpr`, `typeof`, and atomic primitives to carve a lean, modern path through the old ways.
- **RISC-V Native:** We target the open silicon—primarily the **Spacemit X1 / OrangePi RV2**. We develop in the shadows of QEMU, but we live on the metal.

## SYSTEM ANATOMY

- **Croi** (The Heart): The Exec kernel. Multitasking without the bloat.
- **Logaic** (The Logic): The DOS. Filesystems that make sense, not hierarchies of bureaucracy.
- **Leargas** (The Vision): The Intuition. Direct management of the visual matrix.
- **Clar** (The Canvas): The Workbench. A desktop for those who still know how to use one.
- **Splanc** (The Spark): The UEFI spark that ignites the machine.

## STATUS: PHASE 0 // THE FOUNDATION

The signal is weak but growing.
- [x] Architectural manifestos and engineering principles.
- [x] Hosted CMake build with C23 enforcement.
- [x] Core data structures (`list.h`, `ring.h`) validated.
- [x] FDT parser operational.
- [ ] Bootable EFI image for RISC-V 64.

See `docs/ROADMAP.md` for the full deployment sequence.

## FORGING THE IMAGE

You need the RISC-V cross-compiler (Clang) and CMake. 

```bash
mkdir build-rv64 && cd build-rv64
cmake -DCMAKE_TOOLCHAIN_FILE=../cmake/toolchain-riscv64.cmake ..
make
```

## THE COMPACT

CaraOS is licensed under the **BSD 2-Clause License**. See `LICENSE`. Every file carries the SPDX mark. No corporate strings attached.
