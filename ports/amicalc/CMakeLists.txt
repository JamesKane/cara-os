# SPDX-License-Identifier: BSD-2-Clause
#
# CaraOS build glue (BSD-2) for the third-party amiCalc port. amicalc.c is
# verbatim upstream (MIT — see PROVENANCE.md / LICENSE). Phase T.4.4: build a
# real AmigaOS GUI application UNEDITED against the CaraOS SDK and run it.
#
# Built with cara_port_flags_c23 (permissive, but -std=gnu23 because the
# generated <proto/*.h> inline stubs use C23 attribute syntax) + cara_user_libc
# (string/stdlib/stdio + the T.4.2 float: sprintf %.15g / strtod) + cara_libm
# (T.4.3 transcendentals) + libcara_user (_start, whole-archived). It uses
# intuition.library + graphics.library via the <clib/*_protos.h> → <proto/*.h>
# inline stubs, so it needs those generated headers (cara_*_gen deps).
add_executable(amicalc amicalc.c)
target_link_libraries(amicalc PRIVATE
    cara_port_flags_c23
    cara_headers
    cara_user_libc
    cara_libm
    libcara_user
)
add_dependencies(amicalc
    cara_exec_lib_gen
    cara_intuition_lib_gen
    cara_graphics_lib_gen
)
target_link_options(amicalc PRIVATE
    -nostdlib
    -fuse-ld=${CARA_RV64_LDLLD_PATH}
    -Wl,-T,${CMAKE_SOURCE_DIR}/src/userland/user.lds
    -Wl,--build-id=none
    -Wl,--whole-archive
    "$<TARGET_FILE:libcara_user>"
    -Wl,--no-whole-archive
)
set_target_properties(amicalc PROPERTIES
    LINK_DEPENDS "${CMAKE_SOURCE_DIR}/src/userland/user.lds"
    LINKER_LANGUAGE C
    SUFFIX ".elf"
    OUTPUT_NAME "amicalc"
)
