# SPDX-License-Identifier: BSD-2-Clause
#
# Configure-time validation that the chosen compiler implements the C23
# features CaraOS depends on. Each check compiles a small snippet under
# `-std=c23 -Werror`; any failure aborts the configure with a clear message.
#
# We check only features the architecture actually relies on. Adding a new
# C23 feature to the codebase should add a probe here.

include(CheckCSourceCompiles)

set(_cara_saved_required_flags "${CMAKE_REQUIRED_FLAGS}")
set(CMAKE_REQUIRED_FLAGS "-std=c23 -Werror")

check_c_source_compiles("
int main(void) { int *p = nullptr; return p ? 1 : 0; }
" CARA_C23_HAS_NULLPTR)

check_c_source_compiles("
constexpr int x = 42;
int main(void) { return x - 42; }
" CARA_C23_HAS_CONSTEXPR)

check_c_source_compiles("
int main(void) { int a = 5; typeof(a) b = a; return b - 5; }
" CARA_C23_HAS_TYPEOF)

check_c_source_compiles("
[[nodiscard]] static int f(void) { return 0; }
int main(void) { return f(); }
" CARA_C23_HAS_NODISCARD)

check_c_source_compiles("
#include <stdatomic.h>
int main(void) {
    _Atomic int a = 0;
    atomic_store_explicit(&a, 1, memory_order_release);
    return atomic_load_explicit(&a, memory_order_acquire) - 1;
}
" CARA_C23_HAS_ATOMICS)

check_c_source_compiles("
struct P { int x, y; };
int main(void) { struct P p = {.x = 3, .y = 4}; return p.x + p.y - 7; }
" CARA_C23_HAS_DESIGNATED_INIT)

set(CMAKE_REQUIRED_FLAGS "${_cara_saved_required_flags}")
unset(_cara_saved_required_flags)

set(_cara_required_features
    NULLPTR CONSTEXPR TYPEOF NODISCARD ATOMICS DESIGNATED_INIT
)

set(_cara_missing "")
foreach(_feat IN LISTS _cara_required_features)
    if(NOT CARA_C23_HAS_${_feat})
        list(APPEND _cara_missing ${_feat})
    endif()
endforeach()

if(_cara_missing)
    message(FATAL_ERROR
        "CaraOS requires a C23 compiler. The following features failed to "
        "compile under -std=c23 with ${CMAKE_C_COMPILER_ID} "
        "${CMAKE_C_COMPILER_VERSION}:\n  ${_cara_missing}\n"
        "Install a newer clang (>= 18 upstream / Apple Clang 16+).")
endif()

unset(_cara_required_features)
unset(_cara_missing)
