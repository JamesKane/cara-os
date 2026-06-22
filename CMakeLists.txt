# SPDX-License-Identifier: BSD-2-Clause
cmake_minimum_required(VERSION 3.25)

project(cara-os
    VERSION 0.0.1
    DESCRIPTION "CaraOS: a C23/RISC-V cleanroom reimplementation of the AmigaOS Release 2 spec"
    LANGUAGES C ASM
)

set(CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/cmake" ${CMAKE_MODULE_PATH})
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Debug CACHE STRING "Default build type" FORCE)
endif()

# --- Target selection --------------------------------------------------------
# CARA_TARGET selects what we are building for in this configure:
#   host    - native build for unit tests + host-side tools
#   riscv64 - cross-build for splanc.efi + croi.elf on RISC-V
# Pick exactly one per build directory; flip directories when you need both.
set(CARA_TARGET "host" CACHE STRING "Cara build target: host or riscv64")
set_property(CACHE CARA_TARGET PROPERTY STRINGS host riscv64)
if(NOT CARA_TARGET STREQUAL "host" AND NOT CARA_TARGET STREQUAL "riscv64")
    message(FATAL_ERROR
        "CARA_TARGET must be 'host' or 'riscv64'; got '${CARA_TARGET}'.")
endif()
message(STATUS "CaraOS target: ${CARA_TARGET}")

# --- Toolchain gate: clang only ----------------------------------------------
# CaraOS targets clang exclusively (Apple Clang on macOS hosts is accepted).
# GCC may follow once the codebase is real, but the C23 surface we use is
# clang-tested first.
if(NOT (CMAKE_C_COMPILER_ID STREQUAL "Clang" OR
        CMAKE_C_COMPILER_ID STREQUAL "AppleClang"))
    message(FATAL_ERROR
        "CaraOS requires clang. CMAKE_C_COMPILER_ID is \"${CMAKE_C_COMPILER_ID}\". "
        "Set CC=clang (or pass -DCMAKE_C_COMPILER=clang) and reconfigure.")
endif()

# --- Language standard -------------------------------------------------------
set(CMAKE_C_STANDARD 23)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)   # -std=c23, not -std=gnu23

# Validate C23 features at configure time.
include(CaraC23Check)

# --- Common compile flags (consumed via INTERFACE library) -------------------
add_library(cara_flags INTERFACE)

target_compile_options(cara_flags INTERFACE
    -std=c23
    -Wall
    -Wextra
    -Wpedantic
    -Wshadow
    -Wstrict-prototypes
    -Wmissing-prototypes
    -Wold-style-definition
    -Wpointer-arith
    -Wcast-qual
    -Wwrite-strings
    -Wundef
    -Wvla
    -Wno-unused-command-line-argument
    -Werror
    -fno-common
    -fstrict-aliasing
)

# --- Public headers ----------------------------------------------------------
add_library(cara_headers INTERFACE)
target_include_directories(cara_headers INTERFACE
    "${CMAKE_SOURCE_DIR}/include"
)

# --- Format targets ----------------------------------------------------------
# `make format`        rewrites all sources in place
# `make format-check`  fails if anything is out of style (CI-friendly)
find_program(CLANG_FORMAT
    NAMES clang-format
    HINTS
        /opt/homebrew/opt/llvm/bin
        /usr/local/opt/llvm/bin
        /usr/lib/llvm-22/bin
        /usr/lib/llvm-21/bin
        /usr/lib/llvm-20/bin
        /usr/lib/llvm-19/bin
        /usr/lib/llvm-18/bin
)

if(CLANG_FORMAT)
    file(GLOB_RECURSE _cara_format_sources
        CONFIGURE_DEPENDS
        "${CMAKE_SOURCE_DIR}/src/*.c"
        "${CMAKE_SOURCE_DIR}/src/*.h"
        "${CMAKE_SOURCE_DIR}/include/*.h"
        "${CMAKE_SOURCE_DIR}/tests/*.c"
        "${CMAKE_SOURCE_DIR}/tests/*.h"
    )
    if(_cara_format_sources)
        add_custom_target(format
            COMMAND "${CLANG_FORMAT}" -i ${_cara_format_sources}
            VERBATIM
            COMMENT "Reformatting ${CMAKE_SOURCE_DIR} with ${CLANG_FORMAT}"
        )
        add_custom_target(format-check
            COMMAND "${CLANG_FORMAT}" --dry-run --Werror ${_cara_format_sources}
            VERBATIM
            COMMENT "Checking format with ${CLANG_FORMAT}"
        )
    endif()
    unset(_cara_format_sources)
else()
    message(STATUS
        "clang-format not found; `format` and `format-check` targets disabled. "
        "Install LLVM (e.g. `brew install llvm`) to enable them.")
endif()

# --- Host-side tools ---------------------------------------------------------
# tools/lvo-gen runs at build time on the host to generate per-library LVO
# stubs from .conf files (see docs/LVO.md). Built only on host; cross-builds
# expect the host build dir to provide the binary.
if(CARA_TARGET STREQUAL "host")
    add_subdirectory(tools/lvo-gen)
endif()

# CMake helper for invoking lvo-gen against per-library .conf files.
# Both host and rv64 builds need it — the host build drives the
# generator directly, the rv64 cross-build invokes a host-built binary
# located via LVO_GEN_BIN. Must be included BEFORE add_subdirectory(src)
# so src/croi/exec_lib/CMakeLists.txt can call cara_lvo_gen_library().
include(CaraLvoGen)

# --- Source tree -------------------------------------------------------------
# src/ contains both shared modules (built always) and target-specific code
# (gated inside src/CMakeLists.txt by CARA_TARGET).
add_subdirectory(src)

# tools/carafs: hosted mkfs.carafs / fsck.carafs shells around the
# shared cara_carafs core (Phase 2 Subgoal 4). After src/ so the core
# library target exists.
if(CARA_TARGET STREQUAL "host")
    add_subdirectory(tools/carafs)
endif()

# --- Phase 9 / aistreoir aggregator -----------------------------------------
# After every cara_lvo_gen_library() call has appended its fragment to the
# pending-list, aggregate them into the single
# include/aistreoir/lvo_table.gen.h that the Phase 9 68k → RV64 translator
# will consume. No consumer exists yet (Phase 9 is far-future), so this is
# forward-investment; the file builds and stays in sync with the .conf set.
cara_lvo_gen_aggregate()

# --- LVO stub-coverage report ------------------------------------------------
# `cmake --build build-host --target lvo-coverage` regenerates
# docs/LVO_COVERAGE.md from the .conf set (single source of truth — see
# docs/PHASE3.md §7 Q4). Host-only; the report is committed so it is
# browsable on GitHub, and regenerated on demand rather than every build.
if(CARA_TARGET STREQUAL "host")
    add_custom_target(lvo-coverage
        COMMAND $<TARGET_FILE:lvo-gen> --coverage
                "${CMAKE_SOURCE_DIR}/docs/LVO_COVERAGE.md"
                "${CMAKE_SOURCE_DIR}/tools/lvo-gen/exec.conf"
                "${CMAKE_SOURCE_DIR}/tools/lvo-gen/intuition.conf"
                "${CMAKE_SOURCE_DIR}/tools/lvo-gen/utility.conf"
                "${CMAKE_SOURCE_DIR}/tools/lvo-gen/dos.conf"
                "${CMAKE_SOURCE_DIR}/tools/lvo-gen/graphics.conf"
                "${CMAKE_SOURCE_DIR}/tools/lvo-gen/gadtools.conf"
                "${CMAKE_SOURCE_DIR}/tools/lvo-gen/asl.conf"
                "${CMAKE_SOURCE_DIR}/tools/lvo-gen/cruth.conf"
        DEPENDS lvo-gen
        VERBATIM
        COMMENT "lvo-gen coverage → docs/LVO_COVERAGE.md")
endif()

# --- Tests -------------------------------------------------------------------
if(CARA_TARGET STREQUAL "host")
    enable_testing()
    add_subdirectory(tests)
endif()
