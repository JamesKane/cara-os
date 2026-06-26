# SPDX-License-Identifier: BSD-2-Clause
#
# Cross-build toolchain for the CaraOS ARM64 (AArch64) kernel — the second
# arch backend behind the HAL (docs/ARM64.md, epic H.7). Configure with
#
#   cmake -S . -B build-arm64 -DCARA_TARGET=riscv64 -DCARA_ARCH=arm64 \
#         -DCMAKE_TOOLCHAIN_FILE=cmake/toolchain-arm64.cmake
#
# (CARA_TARGET=riscv64 keeps its carve-out meaning "cross-build the kernel";
# CARA_ARCH=arm64 names the ISA — see docs/ARCH_HAL.md §2.) We require the same
# Homebrew LLVM 22+ (clang + lld + llvm-objcopy) as the RV64 toolchain; Apple
# Clang cannot cross-link.

set(CMAKE_SYSTEM_NAME Generic)
set(CMAKE_SYSTEM_PROCESSOR aarch64)

# Pin to Homebrew LLVM if present; fall back to whatever is on PATH.
set(_cara_llvm_hints
    "/opt/homebrew/opt/llvm/bin"
    "/usr/local/opt/llvm/bin"
    "/usr/lib/llvm-22/bin"
    "/usr/lib/llvm-21/bin"
    "/usr/lib/llvm-20/bin"
    "/usr/lib/llvm-19/bin"
    "/usr/lib/llvm-18/bin"
)
set(_cara_lld_hints
    "/opt/homebrew/opt/lld/bin"
    "/usr/local/opt/lld/bin"
)

find_program(CARA_ARM64_CLANG   NAMES clang        HINTS ${_cara_llvm_hints})
find_program(CARA_ARM64_LDLLD   NAMES ld.lld       HINTS ${_cara_lld_hints} ${_cara_llvm_hints})
find_program(CARA_ARM64_OBJCOPY NAMES llvm-objcopy HINTS ${_cara_llvm_hints})
find_program(CARA_ARM64_AR      NAMES llvm-ar      HINTS ${_cara_llvm_hints})
find_program(CARA_ARM64_RANLIB  NAMES llvm-ranlib  HINTS ${_cara_llvm_hints})
find_program(CARA_ARM64_READELF NAMES llvm-readelf HINTS ${_cara_llvm_hints})
find_program(CARA_ARM64_OBJDUMP NAMES llvm-objdump HINTS ${_cara_llvm_hints})

if(NOT CARA_ARM64_CLANG)
    message(FATAL_ERROR
        "ARM64 toolchain: clang not found. Install LLVM (e.g. `brew install llvm`).")
endif()
if(NOT CARA_ARM64_LDLLD)
    message(FATAL_ERROR
        "ARM64 toolchain: ld.lld not found. Install lld (e.g. `brew install lld`).")
endif()
if(NOT CARA_ARM64_OBJCOPY)
    message(FATAL_ERROR
        "ARM64 toolchain: llvm-objcopy not found. Install LLVM tools.")
endif()
if(NOT CARA_ARM64_RANLIB)
    message(FATAL_ERROR
        "ARM64 toolchain: llvm-ranlib not found. Install LLVM tools.")
endif()

set(CMAKE_C_COMPILER          "${CARA_ARM64_CLANG}")
set(CMAKE_ASM_COMPILER        "${CARA_ARM64_CLANG}")
set(CMAKE_C_COMPILER_TARGET   aarch64-unknown-elf)
set(CMAKE_ASM_COMPILER_TARGET aarch64-unknown-elf)
set(CMAKE_AR      "${CARA_ARM64_AR}"      CACHE FILEPATH "")
set(CMAKE_RANLIB  "${CARA_ARM64_RANLIB}"  CACHE FILEPATH "")
set(CMAKE_OBJCOPY "${CARA_ARM64_OBJCOPY}" CACHE FILEPATH "")
set(CMAKE_OBJDUMP "${CARA_ARM64_OBJDUMP}" CACHE FILEPATH "")

# Skip the dynamic-link compiler test — there's no libc to link against.
set(CMAKE_C_COMPILER_WORKS 1)
set(CMAKE_ASM_COMPILER_WORKS 1)
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)

# Defaults for downstream targets. The kernel is integer-only
# (-mgeneral-regs-only — no FP/SIMD in S-mode codegen, the AArch64 analogue of
# the RV64 soft-float kernel); U-mode FP/NEON is enabled lazily later (H.7.4).
set(CARA_ARM64_KERNEL_ARCH "armv8-a"    CACHE STRING "AArch64 march for kernel")
set(CARA_ARM64_USER_ARCH   "armv8-a"    CACHE STRING "AArch64 march for userspace")

# Path to ld.lld so the kernel link can pass it to clang via -fuse-ld=.
set(CARA_ARM64_LDLLD_PATH "${CARA_ARM64_LDLLD}" CACHE FILEPATH "ld.lld for ELF link")
