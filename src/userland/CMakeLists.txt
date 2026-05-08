# SPDX-License-Identifier: BSD-2-Clause
#
# userland: cross-compiled U-mode programs that the kernel embeds and
# spawns. Each target is a static, freestanding ELF linked at user VAs
# starting at 0x10000.

set(_user_lds "${CMAKE_CURRENT_SOURCE_DIR}/user.lds")

add_executable(userhello userhello.c)
target_link_libraries(userhello PRIVATE cara_kernel_flags cara_headers)
target_link_options(userhello PRIVATE
    -nostdlib
    -fuse-ld=${CARA_RV64_LDLLD_PATH}
    -Wl,-T,${_user_lds}
    -Wl,--build-id=none
)
set_target_properties(userhello PROPERTIES
    LINK_DEPENDS ${_user_lds}
    LINKER_LANGUAGE C
    SUFFIX ".elf"
    OUTPUT_NAME "userhello"
)
