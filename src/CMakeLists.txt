# SPDX-License-Identifier: BSD-2-Clause
#
# src/ houses both kernel-side code (croi, splanc) and shared modules that
# build for either target. The FDT parser is the canonical example: it
# compiles unchanged into the kernel image and into host unit tests.

if(CARA_TARGET STREQUAL "riscv64")
    # Common flags for any code linked into freestanding RV64 binaries
    # (croi.elf, splanc.efi). Freestanding, soft-float, medany code model.
    # Userspace eventually flips to lp64d + RVV; the kernel stays soft-float.
    add_library(cara_kernel_flags INTERFACE)
    target_compile_options(cara_kernel_flags INTERFACE
        -ffreestanding
        -nostdlibinc                # keep <stdarg.h>, <stddef.h>
        -fno-stack-protector
        -fno-builtin
        -mno-relax
        -mcmodel=medany
        -march=${CARA_RV64_KERNEL_ARCH}
        -mabi=${CARA_RV64_KERNEL_ABI}
        -Wall
        -Wextra
        -Wpedantic
        -Wshadow
        -Werror
        -fno-common
        -fstrict-aliasing
    )
    target_compile_definitions(cara_kernel_flags INTERFACE CARA_FREESTANDING=1)
endif()

# Always-on: shared modules that compile in both worlds.
add_subdirectory(croi/fdt)
add_subdirectory(croi/mm)

# Always-on extra: dath builds for both targets (host tests verify the
# FDT parser; the kernel uses the full module).
add_subdirectory(croi/dath)

# Always-on: PCIe enumeration. Host build is FDT parser only;
# kernel build adds the ECAM accessors + bus walk.
add_subdirectory(croi/pci)

# Always-on: HID boot-protocol decoders (HA.2/3) + USB Usage → V36+
# rawkey table (HA.4). Pure logic, no I/O — host unit tests cover
# the decode invariants; the kernel substrate links the same .o.
add_subdirectory(croi/hid)

if(CARA_TARGET STREQUAL "riscv64")
    add_subdirectory(croi/log)
    add_subdirectory(croi/util)
    add_subdirectory(croi/kobj)
    add_subdirectory(croi/loader)
    add_subdirectory(croi/sched)
    add_subdirectory(croi/ipc)
    add_subdirectory(croi/exec_lib)
    add_subdirectory(croi/xhci)
    add_subdirectory(croi/syscall)
    add_subdirectory(userland)
    add_subdirectory(croi/tests)
    add_subdirectory(croi)
    # add_subdirectory(splanc)   # arrives in real-hardware slice
endif()
