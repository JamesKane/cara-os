# SPDX-License-Identifier: BSD-2-Clause
#
# cara_exec_lib: kernel-side V36+ exec.library impl helpers.
# Each Croi_*_Impl below is what src/croi/syscall/syscall.c routes
# the matching SYS_* arm to. The user-facing trampolines that initiate
# those syscalls live in trampolines.S and the per-library .conf
# generated artefacts.

add_library(cara_exec_lib STATIC
    mem.c
    signal.c
    ipc.c
    library_registry.c
    make_library.c
    open_library.c
    exec_hooks.c
    image.c
    trampolines.S
)
target_link_libraries(cara_exec_lib
    PUBLIC cara_headers
    PRIVATE cara_kernel_flags
)

# Run lvo-gen against tools/lvo-gen/exec.conf to produce the four
# artefact families described in docs/LVO.md §10.2:
#   ${CARA_LVO_GEN_OUT_DIR}/include/proto/exec.h        — inline stubs
#   ${CARA_LVO_GEN_OUT_DIR}/include/exec/lvo.h          — CARA_IDX_*/_LVO*
#   ${CARA_LVO_GEN_OUT_DIR}/src/croi/exec_lib/exec_vec.c — vec table source
#   ${CARA_LVO_GEN_OUT_DIR}/aistreoir/exec.inc          — Phase 9 fragment
cara_lvo_gen_library("${CMAKE_SOURCE_DIR}/tools/lvo-gen/exec.conf")

if(CARA_LVO_GEN_BIN)
    set(_exec_vec_c "${CARA_LVO_GEN_OUT_DIR}/src/croi/exec_lib/exec_vec.c")
    target_sources(cara_exec_lib PRIVATE "${_exec_vec_c}")
    add_dependencies(cara_exec_lib cara_exec_lib_gen)
endif()
