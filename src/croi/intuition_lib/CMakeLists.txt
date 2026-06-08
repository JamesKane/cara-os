# SPDX-License-Identifier: BSD-2-Clause
#
# cara_intuition_lib: kernel-side V36+ intuition.library bridge.
# Mirrors src/croi/exec_lib. The Croi_*_Impl bodies (bridge.c) are what
# src/croi/syscall/syscall.c routes the SYS_* arms to; the user-facing
# trampolines (trampolines.S) initiate those syscalls; the generated
# intuition_vec.c is the negative-side function-pointer table that
# Croi_MakeLibrary copies into the library base.
#
# lvo-gen emits the artefact families (docs/LVO.md §10.2):
#   ${CARA_LVO_GEN_OUT_DIR}/include/proto/intuition.h        — inline stubs
#   ${CARA_LVO_GEN_OUT_DIR}/include/intuition/lvo.h          — CARA_IDX_*/_LVO*
#   ${CARA_LVO_GEN_OUT_DIR}/src/croi/intuition_lib/intuition_vec.c
#   ${CARA_LVO_GEN_OUT_DIR}/aistreoir/intuition.inc          — Phase 9 fragment
cara_lvo_gen_library("${CMAKE_SOURCE_DIR}/tools/lvo-gen/intuition.conf")

add_library(cara_intuition_lib STATIC
    bridge.c
    intuition_hooks.c
    trampolines.S
)
target_link_libraries(cara_intuition_lib
    PUBLIC cara_headers
    PRIVATE cara_kernel_flags
)

if(CARA_LVO_GEN_BIN)
    set(_intuition_vec_c "${CARA_LVO_GEN_OUT_DIR}/src/croi/intuition_lib/intuition_vec.c")
    target_sources(cara_intuition_lib PRIVATE "${_intuition_vec_c}")
    add_dependencies(cara_intuition_lib cara_intuition_lib_gen)
endif()
