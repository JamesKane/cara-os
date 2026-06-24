# SPDX-License-Identifier: BSD-2-Clause
function(cara_unit_test name)
    add_executable(${name} ${name}.c)
    target_link_libraries(${name} PRIVATE cara_flags cara_headers ${ARGN})
    add_test(NAME ${name} COMMAND ${name})
endfunction()

cara_unit_test(test_c23)
cara_unit_test(test_list)
cara_unit_test(test_ring)

# test_libc_float: the userland libc float bits (fmt.c %f/%e/%g + strtod.c)
# are pure (no syscalls), so compile them straight into a host test. T.4.2.
add_executable(test_libc_float
    test_libc_float.c
    "${CMAKE_SOURCE_DIR}/src/userland/libc/fmt.c"
    "${CMAKE_SOURCE_DIR}/src/userland/libc/strtod.c"
)
target_include_directories(test_libc_float PRIVATE "${CMAKE_SOURCE_DIR}/src/userland/libc")
target_link_libraries(test_libc_float PRIVATE cara_flags cara_headers)
add_test(NAME test_libc_float COMMAND test_libc_float)

cara_unit_test(test_fdt_header cara_fdt)
cara_unit_test(test_fdt_walk   cara_fdt)
cara_unit_test(test_fdt_qemu   cara_fdt)
cara_unit_test(test_physmap    cara_mm cara_fdt)
cara_unit_test(test_dath_fdt   cara_dath cara_fdt)
cara_unit_test(test_pci_fdt    cara_pci  cara_fdt)
cara_unit_test(test_hid        cara_hid)
cara_unit_test(test_carafs_format cara_carafs)
cara_unit_test(test_carafs_mkfs   cara_carafs)
cara_unit_test(test_carafs_file   cara_carafs)
cara_unit_test(test_carafs_dir    cara_carafs)
cara_unit_test(test_carafs_journal cara_carafs)
cara_unit_test(test_carafs_xattr  cara_carafs)
cara_unit_test(test_gpt           cara_gpt)
cara_unit_test(test_leargas_input cara_leargas)
cara_unit_test(test_leargas_pointer cara_leargas)
cara_unit_test(test_leargas_screen cara_leargas)
cara_unit_test(test_leargas_router cara_leargas)
cara_unit_test(test_leargas_window cara_leargas)
cara_unit_test(test_leargas_focus cara_leargas)
cara_unit_test(test_leargas_idcmp cara_leargas)
cara_unit_test(test_leargas_gadget cara_leargas)
cara_unit_test(test_leargas_string cara_leargas)
cara_unit_test(test_leargas_button cara_leargas)
target_compile_definitions(test_fdt_qemu PRIVATE
    CARA_TEST_DATADIR="${CMAKE_SOURCE_DIR}/tests/data"
)
target_compile_definitions(test_physmap PRIVATE
    CARA_TEST_DATADIR="${CMAKE_SOURCE_DIR}/tests/data"
)
target_compile_definitions(test_pci_fdt PRIVATE
    CARA_TEST_DATADIR="${CMAKE_SOURCE_DIR}/tests/data"
)
