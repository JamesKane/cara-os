# SPDX-License-Identifier: BSD-2-Clause
#
# cara_expansion_lib: kernel-side V36+ expansion.library bridge (L14).
# Mirrors src/croi/icon: the Croi_Exp_*_Impl bodies (expansion_lib.c) are
# what syscall.c routes the SYS_Exp_* arms to; trampolines.S initiates those
# syscalls; the generated expansion_vec.c is the negative-side vec table
# Croi_MakeLibrary copies into the library base. L14.1 ships the ConfigDev
# list ops; the list is the FDT/PCI boot inventory (g_pci_inv) presented as
# ConfigDevs, in the SASOS shared heap. riscv64-only; whole-archived into
# croi by src/croi/CMakeLists.txt.
cara_lvo_gen_library("${CMAKE_SOURCE_DIR}/tools/lvo-gen/expansion.conf")

add_library(cara_expansion_lib STATIC
    expansion_hooks.c
    expansion_lib.c
    trampolines.S
)
target_link_libraries(cara_expansion_lib
    PUBLIC cara_headers
    PRIVATE cara_kernel_flags
)

if(CARA_LVO_GEN_BIN)
    set(_exp_vec_c "${CARA_LVO_GEN_OUT_DIR}/src/croi/expansion/expansion_vec.c")
    target_sources(cara_expansion_lib PRIVATE "${_exp_vec_c}")
    add_dependencies(cara_expansion_lib cara_expansion_lib_gen)
endif()
