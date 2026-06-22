# SPDX-License-Identifier: BSD-2-Clause
#
# cara_asl_lib: kernel-side V36+ asl.library bridge (L9). Mirrors
# src/croi/gadtools: the Croi_Asl_*_Impl bodies (asl_lib.c) are what
# syscall.c routes the SYS_Asl_* arms to; trampolines.S initiates those
# syscalls; the generated asl_vec.c is the negative-side vec table
# Croi_MakeLibrary copies into the library base. riscv64-only;
# whole-archived into croi by src/croi/CMakeLists.txt.
cara_lvo_gen_library("${CMAKE_SOURCE_DIR}/tools/lvo-gen/asl.conf")

add_library(cara_asl_lib STATIC
    asl_hooks.c
    asl_lib.c
    trampolines.S
)
target_link_libraries(cara_asl_lib
    PUBLIC cara_headers
    PRIVATE cara_kernel_flags
)

if(CARA_LVO_GEN_BIN)
    set(_asl_vec_c "${CARA_LVO_GEN_OUT_DIR}/src/croi/asl/asl_vec.c")
    target_sources(cara_asl_lib PRIVATE "${_asl_vec_c}")
    add_dependencies(cara_asl_lib cara_asl_lib_gen)
endif()
