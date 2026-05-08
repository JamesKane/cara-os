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

if(CARA_TARGET STREQUAL "riscv64")
    add_subdirectory(croi)
    # add_subdirectory(splanc)   # arrives in task #11
endif()
