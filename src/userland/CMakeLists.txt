# SPDX-License-Identifier: BSD-2-Clause
#
# userland: cross-compiled U-mode programs that the kernel embeds and
# spawns. Each target is a static, freestanding ELF linked at user VAs
# starting at 0x10000.

set(_user_lds "${CMAKE_CURRENT_SOURCE_DIR}/user.lds")

# libcara — minimal U-mode C runtime. Provides _start that
# bootstrap-opens exec.library via inline ecall (the SysBase global
# isn't valid yet, so the LVO inline stubs can't be used), assigns
# the SysBase global, calls main, then SYS_EXIT on return.
add_library(libcara_user STATIC libcara_init.c)
target_link_libraries(libcara_user PRIVATE cara_kernel_flags cara_headers)

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

# userexec — LVO.md §9 worked example. Links against libcara_user
# and against the generated proto/exec.h's inline stubs (no actual
# library link is needed for those — the stubs are static inline,
# the only external symbol they reference is SysBase, which libcara
# defines).
add_executable(userexec userexec.c)
target_link_libraries(userexec PRIVATE
    cara_kernel_flags
    cara_headers
    libcara_user
)
add_dependencies(userexec cara_exec_lib_gen)
target_link_options(userexec PRIVATE
    -nostdlib
    -fuse-ld=${CARA_RV64_LDLLD_PATH}
    -Wl,-T,${_user_lds}
    -Wl,--build-id=none
    # Pull all of libcara_user — _start is the ELF entry but no other
    # symbol references it directly, so static-archive GC would drop
    # the .o without --whole-archive. (CMake's target_link_libraries
    # otherwise applies linker-default --as-needed semantics.)
    -Wl,--whole-archive
    "$<TARGET_FILE:libcara_user>"
    -Wl,--no-whole-archive
)
set_target_properties(userexec PROPERTIES
    LINK_DEPENDS ${_user_lds}
    LINKER_LANGUAGE C
    SUFFIX ".elf"
    OUTPUT_NAME "userexec"
)
