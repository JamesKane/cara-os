# SPDX-License-Identifier: BSD-2-Clause
#
# cara_dos_lib: kernel-side V36+ dos.library (AmigaDOS). Owned by Logaic
# (docs/LOGAIC_DOS.md). L3.1 is the ABI foundation: the reserved hooks
# (dos_hooks.c), the IoErr syscall impl (io_err.c) + its trampoline
# (trampolines.S, .lib_text.dos RX page), and the generated vec table.
# The file/lock/dir packet ops (server flavour, in the dos handler Gleas)
# arrive with later L3 slices.
#
# lvo-gen emits the artefact families (docs/LVO.md §10.2):
#   ${CARA_LVO_GEN_OUT_DIR}/include/proto/dos.h            — inline stubs
#   ${CARA_LVO_GEN_OUT_DIR}/include/dos/lvo.h              — CARA_IDX_*/_LVO*
#   ${CARA_LVO_GEN_OUT_DIR}/src/logaic/dos/dos_vec.c
#   ${CARA_LVO_GEN_OUT_DIR}/aistreoir/dos.inc             — Phase 9 fragment
cara_lvo_gen_library("${CMAKE_SOURCE_DIR}/tools/lvo-gen/dos.conf")

add_library(cara_dos_lib STATIC
    io_err.c
    handler.c
    ops.c
    loadseg.c
    dos_hooks.c
    trampolines.S
)
target_link_libraries(cara_dos_lib
    PUBLIC cara_headers
    PRIVATE cara_kernel_flags
)

if(CARA_LVO_GEN_BIN)
    set(_dos_vec_c "${CARA_LVO_GEN_OUT_DIR}/src/logaic/dos/dos_vec.c")
    target_sources(cara_dos_lib PRIVATE "${_dos_vec_c}")
    add_dependencies(cara_dos_lib cara_dos_lib_gen)
endif()
