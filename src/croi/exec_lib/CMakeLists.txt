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
    trampolines.S
)
target_link_libraries(cara_exec_lib
    PUBLIC cara_headers
    PRIVATE cara_kernel_flags
)
