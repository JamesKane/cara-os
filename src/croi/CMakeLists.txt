# SPDX-License-Identifier: BSD-2-Clause
#
# croi: the CaraOS microkernel ELF. Built only when CARA_TARGET=riscv64;
# parent src/CMakeLists.txt gates this directory and provides
# cara_kernel_flags.
set(_croi_lds "${CMAKE_CURRENT_SOURCE_DIR}/croi.lds")

set(_user_blob_s "${CMAKE_CURRENT_BINARY_DIR}/user_blob.S")
set(_userhello_elf "${CMAKE_BINARY_DIR}/src/userland/userhello.elf")
set(_userexec_elf  "${CMAKE_BINARY_DIR}/src/userland/userexec.elf")
set(_userintuition_elf "${CMAKE_BINARY_DIR}/src/userland/userintuition.elf")
set(_clar_elf      "${CMAKE_BINARY_DIR}/src/userland/clar.elf")
set(_v36hello_elf  "${CMAKE_BINARY_DIR}/src/userland/v36hello.elf")
file(WRITE ${_user_blob_s}
"// Auto-generated. Embeds userhello.elf and userexec.elf into croi.elf
// under the .user_elf section. Each ELF is wrapped in start/end
// symbols so kernel-side test code can hand the loader a span.
.section .user_elf, \"a\"
.balign 8
.globl __userhello_elf_start
.globl __userhello_elf_end
__userhello_elf_start:
.incbin \"${_userhello_elf}\"
__userhello_elf_end:
.balign 8
.globl __userexec_elf_start
.globl __userexec_elf_end
__userexec_elf_start:
.incbin \"${_userexec_elf}\"
__userexec_elf_end:
.balign 8
.globl __userintuition_elf_start
.globl __userintuition_elf_end
__userintuition_elf_start:
.incbin \"${_userintuition_elf}\"
__userintuition_elf_end:
.balign 8
.globl __clar_elf_start
.globl __clar_elf_end
__clar_elf_start:
.incbin \"${_clar_elf}\"
__clar_elf_end:
.balign 8
.globl __v36hello_elf_start
.globl __v36hello_elf_end
__v36hello_elf_start:
.incbin \"${_v36hello_elf}\"
__v36hello_elf_end:
")
set_source_files_properties(${_user_blob_s} PROPERTIES
    OBJECT_DEPENDS "${_userhello_elf};${_userexec_elf};${_userintuition_elf};${_clar_elf};${_v36hello_elf}"
)

add_executable(croi
    _start.S
    trap_entry.S
    user_program.S
    ${_user_blob_s}
    entry.c
    trap.c
    time.c
    print.c
    panic.c
    sbi.c
    string.c
    ns16550.c
    platform_init.c
    ramfb.c
    input_pump.c
    carafs_bind.c
)
add_dependencies(croi userhello userexec userintuition clar v36hello)
# Make the Phase 9 aggregated lookup table build alongside croi.elf
# even though no current code references it — keeps the artefact
# in sync with the active .conf set so when Phase 9 lands the
# binary translator can drop the file in without surprises.
if(TARGET cara_aistreoir_table)
    add_dependencies(croi cara_aistreoir_table)
endif()
# intuition.library (I2): the bridge static lib is whole-archived (like
# cara_exec_lib) so trampolines.S.obj and the generated vec table aren't
# GC'd — kernel code never references the trampolines directly; only the
# user-mode vec table at 0x4000_0000+ does.
target_link_libraries(croi PRIVATE
    cara_kernel_flags
    cara_headers
    cara_fdt
    cara_mm
    cara_log
    cara_util
    cara_kobj
    cara_sched
    cara_ipc
    cara_syscall
    cara_loader
    cara_dath
    cara_pci
    cara_xhci
    cara_nvme
    cara_carafs
    cara_gpt
    cara_hid
    cara_leargas
    cara_devices
    # cara_exec_lib is whole-archived so trampolines.S.obj isn't
    # dropped — kernel-mode code never references the trampolines
    # directly (only the user-mode vec table at 0x4000_0000+ does),
    # so without this they'd be GC'd from the static archive. Same
    # pattern as cara_kernel_tests just below.
    -Wl,--whole-archive
    cara_exec_lib
    cara_intuition_lib
    cara_utility_lib
    cara_dos_lib
    cara_graphics_lib
    cara_kernel_tests
    -Wl,--no-whole-archive
)
target_link_options(croi PRIVATE
    -nostdlib
    -fuse-ld=${CARA_RV64_LDLLD_PATH}
    -Wl,-T,${_croi_lds}
    -Wl,--build-id=none
)
set_target_properties(croi PROPERTIES
    LINK_DEPENDS ${_croi_lds}
    LINKER_LANGUAGE C
    SUFFIX ".elf"
    OUTPUT_NAME "croi"
)
