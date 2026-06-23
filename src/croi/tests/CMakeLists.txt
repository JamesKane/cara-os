# SPDX-License-Identifier: BSD-2-Clause
#
# In-kernel self-tests. Each translation unit registers KERNEL_TEST()
# entries into the .kernel_tests linker section; the runner walks them
# at boot. Built only for riscv64.

add_library(cara_kernel_tests STATIC
    runner.c
    test_mm.c
    test_time.c
    test_sched.c
    test_signal.c
    test_handle.c
    test_ipc.c
    test_paging.c
    test_usermode.c
    test_userelf.c
    test_dath.c
    test_graphics.c
    test_intuition.c
    test_gadtools.c
    test_asl.c
    test_iffparse.c
    test_icon.c
    test_diskfont.c
    test_device.c
    test_exec_lib.c
    test_userexec.c
    test_userintuition.c
    test_v36hello.c
    test_clar.c
    test_pci.c
    test_xhci.c
    test_nvme.c
    test_carafs.c
    test_idcmp.c
    test_string.c
    test_shared.c
)
target_include_directories(cara_kernel_tests PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/.."
)
target_link_libraries(cara_kernel_tests
    PUBLIC cara_headers
    PRIVATE cara_kernel_flags
)
