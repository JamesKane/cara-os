# SPDX-License-Identifier: BSD-2-Clause
#
# cara_gadtools_lib: kernel-side V36+ gadtools.library bridge (L8).
# Mirrors src/croi/intuition_lib and src/croi/dath's graphics_lib: the
# Croi_GT_*_Impl bodies (gadtools_lib.c) are what syscall.c routes the
# SYS_GT_* arms to; trampolines.S initiates those syscalls; the generated
# gadtools_vec.c is the negative-side function-pointer table that
# Croi_MakeLibrary copies into the library base. riscv64-only;
# whole-archived into croi by src/croi/CMakeLists.txt so the trampolines
# + vec aren't GC'd.
cara_lvo_gen_library("${CMAKE_SOURCE_DIR}/tools/lvo-gen/gadtools.conf")

add_library(cara_gadtools_lib STATIC
    gadtools_hooks.c
    gadtools_lib.c
    trampolines.S
)
target_link_libraries(cara_gadtools_lib
    PUBLIC cara_headers
    PRIVATE cara_kernel_flags
)

if(CARA_LVO_GEN_BIN)
    set(_gt_vec_c "${CARA_LVO_GEN_OUT_DIR}/src/croi/gadtools/gadtools_vec.c")
    target_sources(cara_gadtools_lib PRIVATE "${_gt_vec_c}")
    add_dependencies(cara_gadtools_lib cara_gadtools_lib_gen)
endif()
