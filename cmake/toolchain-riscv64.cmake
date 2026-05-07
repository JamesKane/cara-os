# SPDX-License-Identifier: BSD-2-Clause
#
# Cross-build toolchain for the CaraOS RV64 target. Configure with
#
#   cmake -S . -B build-rv64 -DCARA_TARGET=riscv64 \
#         -DCMAKE_TOOLCHAIN_FILE=cmake/toolchain-riscv64.cmake
#
# We require Homebrew LLVM 22+ (clang + lld + llvm-objcopy). Apple Clang
# does not ship lld, so it cannot cross-link the kernel.

set(CMAKE_SYSTEM_NAME Generic)
set(CMAKE_SYSTEM_PROCESSOR riscv64)

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

find_program(CARA_RV64_CLANG NAMES clang HINTS ${_cara_llvm_hints})
find_program(CARA_RV64_LDLLD NAMES ld.lld HINTS ${_cara_lld_hints} ${_cara_llvm_hints})
find_program(CARA_RV64_LLDLINK NAMES lld-link HINTS ${_cara_lld_hints} ${_cara_llvm_hints})
find_program(CARA_RV64_OBJCOPY NAMES llvm-objcopy HINTS ${_cara_llvm_hints})
find_program(CARA_RV64_AR      NAMES llvm-ar      HINTS ${_cara_llvm_hints})
find_program(CARA_RV64_RANLIB  NAMES llvm-ranlib  HINTS ${_cara_llvm_hints})
find_program(CARA_RV64_READELF NAMES llvm-readelf HINTS ${_cara_llvm_hints})
find_program(CARA_RV64_OBJDUMP NAMES llvm-objdump HINTS ${_cara_llvm_hints})

if(NOT CARA_RV64_CLANG)
    message(FATAL_ERROR
        "RV64 toolchain: clang not found. Install LLVM (e.g. `brew install llvm`).")
endif()
if(NOT CARA_RV64_LDLLD)
    message(FATAL_ERROR
        "RV64 toolchain: ld.lld not found. Install lld (e.g. `brew install lld`).")
endif()
if(NOT CARA_RV64_OBJCOPY)
    message(FATAL_ERROR
        "RV64 toolchain: llvm-objcopy not found. Install LLVM tools.")
endif()
if(NOT CARA_RV64_RANLIB)
    message(FATAL_ERROR
        "RV64 toolchain: llvm-ranlib not found. Install LLVM tools.")
endif()

set(CMAKE_C_COMPILER        "${CARA_RV64_CLANG}")
set(CMAKE_ASM_COMPILER      "${CARA_RV64_CLANG}")
set(CMAKE_C_COMPILER_TARGET riscv64-unknown-elf)
set(CMAKE_ASM_COMPILER_TARGET riscv64-unknown-elf)
set(CMAKE_AR        "${CARA_RV64_AR}"      CACHE FILEPATH "")
set(CMAKE_RANLIB    "${CARA_RV64_RANLIB}"  CACHE FILEPATH "")
set(CMAKE_OBJCOPY   "${CARA_RV64_OBJCOPY}" CACHE FILEPATH "")
set(CMAKE_OBJDUMP   "${CARA_RV64_OBJDUMP}" CACHE FILEPATH "")

# Skip the dynamic-link compiler test — there's no libc to link against.
set(CMAKE_C_COMPILER_WORKS 1)
set(CMAKE_ASM_COMPILER_WORKS 1)
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)

# Defaults for downstream targets. Kernel/Splanc keep FP/V off and use
# the soft-float ABI so we don't have to manage FS/VS lazy save in early
# boot. Userspace will switch to lp64d once it lands.
set(CARA_RV64_KERNEL_ARCH "rv64imafdc" CACHE STRING "RV64 march for kernel/Splanc")
set(CARA_RV64_KERNEL_ABI  "lp64"       CACHE STRING "RV64 mabi for kernel/Splanc")
set(CARA_RV64_USER_ARCH   "rv64gcv"    CACHE STRING "RV64 march for userspace")
set(CARA_RV64_USER_ABI    "lp64d"      CACHE STRING "RV64 mabi for userspace")

# Path to ld.lld so cara_add_freestanding_lib can pass it to clang via
# -fuse-ld=. lld-link is exposed for the EFI/PE link path (Phase 1 step 11+);
# we record it now so it's discoverable when the time comes.
set(CARA_RV64_LDLLD_PATH   "${CARA_RV64_LDLLD}"   CACHE FILEPATH "ld.lld for ELF link")
set(CARA_RV64_LDLINK_PATH  "${CARA_RV64_LDLINK}"  CACHE FILEPATH "lld-link for PE/EFI link")
