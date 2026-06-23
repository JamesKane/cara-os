# SPDX-License-Identifier: BSD-2-Clause
#
# cara_diskfont_lib: kernel-side V36+ diskfont.library bridge (L12).
# Mirrors src/croi/icon: the Croi_Diskfont_*_Impl bodies (diskfont_lib.c)
# are what syscall.c routes the SYS_Diskfont_* arms to; trampolines.S
# initiates those syscalls; the generated diskfont_vec.c is the
# negative-side vec table Croi_MakeLibrary copies into the library base.
# OpenDiskFont loads a Cara font file off FONTS: into a graphics TextFont
# (FPF_DISKFONT) in the SASOS shared heap; Dath renders it from the strike.
# riscv64-only; whole-archived into croi by src/croi/CMakeLists.txt.
cara_lvo_gen_library("${CMAKE_SOURCE_DIR}/tools/lvo-gen/diskfont.conf")

add_library(cara_diskfont_lib STATIC
    diskfont_hooks.c
    diskfont_lib.c
    trampolines.S
)
target_link_libraries(cara_diskfont_lib
    PUBLIC cara_headers
    PRIVATE cara_kernel_flags
)

if(CARA_LVO_GEN_BIN)
    set(_diskfont_vec_c "${CARA_LVO_GEN_OUT_DIR}/src/croi/diskfont/diskfont_vec.c")
    target_sources(cara_diskfont_lib PRIVATE "${_diskfont_vec_c}")
    add_dependencies(cara_diskfont_lib cara_diskfont_lib_gen)
endif()
