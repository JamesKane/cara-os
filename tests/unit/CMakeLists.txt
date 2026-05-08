# SPDX-License-Identifier: BSD-2-Clause
function(cara_unit_test name)
    add_executable(${name} ${name}.c)
    target_link_libraries(${name} PRIVATE cara_flags cara_headers ${ARGN})
    add_test(NAME ${name} COMMAND ${name})
endfunction()

cara_unit_test(test_c23)
cara_unit_test(test_list)
cara_unit_test(test_ring)

cara_unit_test(test_fdt_header cara_fdt)
cara_unit_test(test_fdt_walk   cara_fdt)
cara_unit_test(test_fdt_qemu   cara_fdt)
cara_unit_test(test_physmap    cara_mm cara_fdt)
target_compile_definitions(test_fdt_qemu PRIVATE
    CARA_TEST_DATADIR="${CMAKE_SOURCE_DIR}/tests/data"
)
target_compile_definitions(test_physmap PRIVATE
    CARA_TEST_DATADIR="${CMAKE_SOURCE_DIR}/tests/data"
)
