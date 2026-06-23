# SPDX-License-Identifier: BSD-2-Clause
#
# CaraOS build glue (BSD-2) for the third-party Dhrystone port (the source
# dhrystone.c is verbatim upstream — see PROVENANCE.md). Phase T: build a
# real AmigaOS-era C program UNEDITED against the CaraOS SDK and run it.
#
# Built with cara_port_flags (permissive -std=gnu89, the source's era) +
# cara_user_libc (string/stdlib/stdio + sys/times.h) + libcara_user (_start,
# whole-archived). It uses no Amiga library API — only the libc + times() —
# so it needs no proto stubs.
add_executable(dhrystone dhrystone.c)
target_link_libraries(dhrystone PRIVATE
    cara_port_flags
    cara_user_libc
)
add_dependencies(dhrystone cara_exec_lib_gen)
target_link_options(dhrystone PRIVATE
    -nostdlib
    -fuse-ld=${CARA_RV64_LDLLD_PATH}
    -Wl,-T,${CMAKE_SOURCE_DIR}/src/userland/user.lds
    -Wl,--build-id=none
    -Wl,--whole-archive
    "$<TARGET_FILE:libcara_user>"
    -Wl,--no-whole-archive
)
set_target_properties(dhrystone PROPERTIES
    LINK_DEPENDS "${CMAKE_SOURCE_DIR}/src/userland/user.lds"
    LINKER_LANGUAGE C
    SUFFIX ".elf"
    OUTPUT_NAME "dhrystone"
)
