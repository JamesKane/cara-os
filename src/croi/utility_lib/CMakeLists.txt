# SPDX-License-Identifier: BSD-2-Clause
#
# cara_utility_lib: kernel-side V36+ utility.library impl. Unlike
# exec/intuition (whose user-callable LVOs are `syscall` flavour and
# trampoline into Croi), every utility.library LVO is `local` — the
# tag-list walkers + Hook dispatcher in tag_ops.c run entirely in U-mode
# from the .lib_text.utility RX page; there is no syscall. utility_hooks.c
# holds the reserved-slot library callbacks (ordinary kernel text).
#
# lvo-gen emits the artefact families (docs/LVO.md §10.2):
#   ${CARA_LVO_GEN_OUT_DIR}/include/proto/utility.h          — inline stubs
#   ${CARA_LVO_GEN_OUT_DIR}/include/utility/lvo.h            — CARA_IDX_*/_LVO*
#   ${CARA_LVO_GEN_OUT_DIR}/src/croi/utility_lib/utility_vec.c
#   ${CARA_LVO_GEN_OUT_DIR}/aistreoir/utility.inc            — Phase 9 fragment
cara_lvo_gen_library("${CMAKE_SOURCE_DIR}/tools/lvo-gen/utility.conf")

add_library(cara_utility_lib STATIC
    tag_ops.c
    tag_alloc.c
    utility_hooks.c
    trampolines.S
)
target_link_libraries(cara_utility_lib
    PUBLIC cara_headers
    PRIVATE cara_kernel_flags
)

if(CARA_LVO_GEN_BIN)
    set(_utility_vec_c "${CARA_LVO_GEN_OUT_DIR}/src/croi/utility_lib/utility_vec.c")
    target_sources(cara_utility_lib PRIVATE "${_utility_vec_c}")
    add_dependencies(cara_utility_lib cara_utility_lib_gen)
endif()
