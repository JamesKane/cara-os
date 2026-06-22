# SPDX-License-Identifier: BSD-2-Clause
#
# cara_iffparse_lib: kernel-side V36+ iffparse.library bridge (L10).
# Mirrors src/croi/asl: the Croi_Iff_*_Impl bodies (iffparse_lib.c) are
# what syscall.c routes the SYS_Iff_* arms to; trampolines.S initiates
# those syscalls; the generated iffparse_vec.c is the negative-side vec
# table Croi_MakeLibrary copies into the library base. riscv64-only;
# whole-archived into croi by src/croi/CMakeLists.txt.
cara_lvo_gen_library("${CMAKE_SOURCE_DIR}/tools/lvo-gen/iffparse.conf")

add_library(cara_iffparse_lib STATIC
    iffparse_hooks.c
    iffparse_lib.c
    trampolines.S
)
target_link_libraries(cara_iffparse_lib
    PUBLIC cara_headers
    PRIVATE cara_kernel_flags
)

if(CARA_LVO_GEN_BIN)
    set(_iff_vec_c "${CARA_LVO_GEN_OUT_DIR}/src/croi/iffparse/iffparse_vec.c")
    target_sources(cara_iffparse_lib PRIVATE "${_iff_vec_c}")
    add_dependencies(cara_iffparse_lib cara_iffparse_lib_gen)
endif()
