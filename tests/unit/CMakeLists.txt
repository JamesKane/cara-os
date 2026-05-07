# SPDX-License-Identifier: BSD-2-Clause
add_executable(test_c23 test_c23.c)
target_link_libraries(test_c23 PRIVATE cara_flags cara_headers)
add_test(NAME test_c23 COMMAND test_c23)
