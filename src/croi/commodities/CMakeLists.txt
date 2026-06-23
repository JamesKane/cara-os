# SPDX-License-Identifier: BSD-2-Clause
#
# cara_commodities_lib: kernel-side V36+ commodities.library bridge (L13).
# Mirrors src/croi/icon: the Croi_Cx_*_Impl bodies (commodities_lib.c) are
# what syscall.c routes the SYS_Cx_* arms to; trampolines.S initiates those
# syscalls; the generated commodities_vec.c is the negative-side vec table
# Croi_MakeLibrary copies into the library base. L13.1 ships the CxObj
# object model (the broker tree) in the SASOS shared heap; live input
# dispatch is deferred. riscv64-only; whole-archived into croi by
# src/croi/CMakeLists.txt.
cara_lvo_gen_library("${CMAKE_SOURCE_DIR}/tools/lvo-gen/commodities.conf")

add_library(cara_commodities_lib STATIC
    commodities_hooks.c
    commodities_lib.c
    trampolines.S
)
target_link_libraries(cara_commodities_lib
    PUBLIC cara_headers
    PRIVATE cara_kernel_flags
)

if(CARA_LVO_GEN_BIN)
    set(_cx_vec_c "${CARA_LVO_GEN_OUT_DIR}/src/croi/commodities/commodities_vec.c")
    target_sources(cara_commodities_lib PRIVATE "${_cx_vec_c}")
    add_dependencies(cara_commodities_lib cara_commodities_lib_gen)
endif()
