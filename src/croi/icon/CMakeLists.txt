# SPDX-License-Identifier: BSD-2-Clause
#
# cara_icon_lib: kernel-side V36+ icon.library bridge (L11). Mirrors
# src/croi/iffparse: the Croi_Icon_*_Impl bodies (icon_lib.c) are what
# syscall.c routes the SYS_Icon_* arms to; trampolines.S initiates those
# syscalls; the generated icon_vec.c is the negative-side vec table
# Croi_MakeLibrary copies into the library base. The DiskObject lives in
# the SASOS shared heap; its source of truth is an object's cara.icon
# xattr (Carafs_Xattr*). riscv64-only; whole-archived into croi by
# src/croi/CMakeLists.txt.
cara_lvo_gen_library("${CMAKE_SOURCE_DIR}/tools/lvo-gen/icon.conf")

add_library(cara_icon_lib STATIC
    icon_hooks.c
    icon_lib.c
    trampolines.S
)
target_link_libraries(cara_icon_lib
    PUBLIC cara_headers
    PRIVATE cara_kernel_flags
)

if(CARA_LVO_GEN_BIN)
    set(_icon_vec_c "${CARA_LVO_GEN_OUT_DIR}/src/croi/icon/icon_vec.c")
    target_sources(cara_icon_lib PRIVATE "${_icon_vec_c}")
    add_dependencies(cara_icon_lib cara_icon_lib_gen)
endif()
