# SPDX-License-Identifier: BSD-2-Clause
#
# croi: the CaraOS microkernel ELF. Built only when CARA_TARGET=riscv64;
# parent src/CMakeLists.txt gates this directory and provides
# cara_kernel_flags.
set(_croi_lds "${CMAKE_CURRENT_SOURCE_DIR}/croi.lds")

add_executable(croi
    _start.S
    trap_entry.S
    entry.c
    trap.c
    time.c
    print.c
    panic.c
    sbi.c
    ns16550.c
    platform_init.c
)
target_link_libraries(croi PRIVATE
    cara_kernel_flags
    cara_headers
    cara_fdt
)
target_link_options(croi PRIVATE
    -nostdlib
    -fuse-ld=${CARA_RV64_LDLLD_PATH}
    -Wl,-T,${_croi_lds}
    -Wl,--build-id=none
)
set_target_properties(croi PROPERTIES
    LINK_DEPENDS ${_croi_lds}
    LINKER_LANGUAGE C
    SUFFIX ".elf"
    OUTPUT_NAME "croi"
)
