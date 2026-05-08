# SPDX-License-Identifier: BSD-2-Clause
#
# tests/lvo-gen — golden-file + negative-case smoke tests for
# tools/lvo-gen. Driven by run_tests.sh; CMake just plumbs the binary
# path and the test directory.

add_test(NAME lvo_gen_smoke
    COMMAND
        "${CMAKE_CURRENT_SOURCE_DIR}/run_tests.sh"
        "$<TARGET_FILE:lvo-gen>"
        "${CMAKE_CURRENT_SOURCE_DIR}"
)
set_tests_properties(lvo_gen_smoke PROPERTIES
    LABELS "lvo-gen"
)
