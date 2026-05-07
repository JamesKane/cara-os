# SPDX-License-Identifier: BSD-2-Clause
#
# Boot smoke tests. Only meaningful when both targets are present —
# ctest runs from the host build, croi.elf is produced by the rv64 build.
# Path to croi.elf is provided via CARA_RV64_BUILD_DIR (cache var); when
# unset the test is skipped at runtime.

find_program(CARA_QEMU_RISCV64 qemu-system-riscv64)

if(NOT CARA_QEMU_RISCV64)
    message(STATUS
        "qemu-system-riscv64 not found; boot smoke tests will be skipped.")
    return()
endif()

set(CARA_RV64_BUILD_DIR "" CACHE PATH
    "Path to the matching CARA_TARGET=riscv64 build dir holding croi.elf")

set(_kernel "${CARA_RV64_BUILD_DIR}/src/croi/croi.elf")

# A convenience target that boots croi under QEMU for interactive use:
#   cmake --build build-host --target qemu-virt-kernel
add_custom_target(qemu-virt-kernel
    COMMAND ${CARA_QEMU_RISCV64}
        -M virt -m 256 -nographic -bios default
        -kernel ${_kernel}
    USES_TERMINAL
    VERBATIM
    COMMENT "Booting Croi under qemu-system-riscv64 -M virt -kernel")

# CTest: only register the test if the rv64 build dir is configured.
# Otherwise the test would always fail with a misleading error.
if(CARA_RV64_BUILD_DIR)
    add_test(NAME smoke_qemu_kernel
        COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/smoke_qemu_kernel.sh
                ${CARA_QEMU_RISCV64} ${_kernel})
    set_tests_properties(smoke_qemu_kernel PROPERTIES
        TIMEOUT 30
        SKIP_RETURN_CODE 77)
else()
    message(STATUS
        "CARA_RV64_BUILD_DIR not set; smoke_qemu_kernel ctest disabled. "
        "Configure with -DCARA_RV64_BUILD_DIR=<path> to enable.")
endif()
