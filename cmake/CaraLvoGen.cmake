# SPDX-License-Identifier: BSD-2-Clause
#
# CaraLvoGen — CMake helper for tools/lvo-gen.
#
# `cara_lvo_gen_library(<conf>)` registers a single CaraOS library
# `.conf` (per docs/LVO.md §4) for code generation. It produces, at
# build time, the four artefact files described in docs/LVO.md §10.2:
#
#   ${CMAKE_BINARY_DIR}/gen/include/proto/<short>.h
#   ${CMAKE_BINARY_DIR}/gen/include/<short>/lvo.h
#   ${CMAKE_BINARY_DIR}/gen/src/<owner>/<short>_vec.c
#   ${CMAKE_BINARY_DIR}/gen/aistreoir/<short>.inc       (per-lib fragment)
#
# `<short>` is the library name with `.library` stripped (so
# `exec.library` → `exec`); `<owner>` is taken from the conf's
# `##owner` directive.
#
# Each call appends to a CMake-scope list `cara_lvo_gen_aistreoir_frags`
# of fragment paths; cara_lvo_gen_aggregate() consumes that list to
# produce the final include/aistreoir/lvo_table.gen.h. Call
# cara_lvo_gen_aggregate() once after all libraries are registered.
#
# The host build (CARA_TARGET=host) drives lvo-gen directly via the
# `lvo-gen` CMake target — the same configure compiles both the tool
# and the .conf consumers. The riscv64 cross-build invokes a host-built
# binary located via the LVO_GEN_BIN cache variable; a typical workflow
# is `cmake -B build-host && cmake --build build-host --target lvo-gen
# && cmake -B build-rv64 -DLVO_GEN_BIN=$PWD/build-host/tools/lvo-gen/lvo-gen`.

if(NOT DEFINED CARA_LVO_GEN_INCLUDED)
    set(CARA_LVO_GEN_INCLUDED 1)

    set(CARA_LVO_GEN_OUT_DIR "${CMAKE_BINARY_DIR}/gen"
        CACHE PATH "Where lvo-gen writes its generated artefacts.")

    if(CARA_TARGET STREQUAL "host")
        # In-configure tool target.
        set(CARA_LVO_GEN_BIN "$<TARGET_FILE:lvo-gen>")
    else()
        # Cross-build: caller must point at a pre-built host binary.
        set(LVO_GEN_BIN "" CACHE FILEPATH
            "Absolute path to a host-built tools/lvo-gen/lvo-gen binary.")
        if(LVO_GEN_BIN AND EXISTS "${LVO_GEN_BIN}")
            set(CARA_LVO_GEN_BIN "${LVO_GEN_BIN}")
        else()
            message(STATUS
                "LVO_GEN_BIN not set or missing; cara_lvo_gen_library() "
                "calls will defer until the build is reconfigured with "
                "-DLVO_GEN_BIN=<path-to-host-lvo-gen>.")
            set(CARA_LVO_GEN_BIN "")
        endif()
    endif()

    # Initialise the list used by cara_lvo_gen_aggregate().
    set(cara_lvo_gen_aistreoir_frags "" CACHE INTERNAL
        "Per-library aistreoir fragments queued for aggregation.")
endif()

# Strip a trailing ".library" from a name; result in ${out_var}.
function(_cara_lvo_short out_var libname)
    string(REGEX REPLACE "\\.library$" "" _short "${libname}")
    set(${out_var} "${_short}" PARENT_SCOPE)
endfunction()

# Read ##library and ##owner directives from a .conf without parsing
# the whole file. Returns ("" "") if either is missing — callers should
# treat that as a configure-time error.
function(_cara_lvo_read_meta conf out_lib out_owner)
    file(READ "${conf}" _content)
    string(REGEX MATCH "##library[ \t]+([A-Za-z0-9_.]+)" _ "${_content}")
    set(_lib "${CMAKE_MATCH_1}")
    string(REGEX MATCH "##owner[ \t]+([A-Za-z0-9_/]+)" _ "${_content}")
    set(_owner "${CMAKE_MATCH_1}")
    set(${out_lib}   "${_lib}"   PARENT_SCOPE)
    set(${out_owner} "${_owner}" PARENT_SCOPE)
endfunction()

function(cara_lvo_gen_library conf_path)
    if(NOT CARA_LVO_GEN_BIN)
        message(WARNING
            "cara_lvo_gen_library('${conf_path}') skipped — "
            "lvo-gen binary not available. (Cross-build needs LVO_GEN_BIN.)")
        return()
    endif()

    if(NOT IS_ABSOLUTE "${conf_path}")
        set(conf_path "${CMAKE_CURRENT_SOURCE_DIR}/${conf_path}")
    endif()
    if(NOT EXISTS "${conf_path}")
        message(FATAL_ERROR "cara_lvo_gen_library: '${conf_path}' not found.")
    endif()

    _cara_lvo_read_meta("${conf_path}" _libname _owner)
    if(NOT _libname OR NOT _owner)
        message(FATAL_ERROR
            "cara_lvo_gen_library: '${conf_path}' is missing "
            "##library or ##owner. (Run lvo-gen --check for details.)")
    endif()
    _cara_lvo_short(_short "${_libname}")

    set(_proto_dir   "${CARA_LVO_GEN_OUT_DIR}/include/proto")
    set(_lvo_dir     "${CARA_LVO_GEN_OUT_DIR}/include/${_short}")
    set(_vec_dir     "${CARA_LVO_GEN_OUT_DIR}/src/${_owner}")
    set(_aistr_dir   "${CARA_LVO_GEN_OUT_DIR}/aistreoir")

    set(_proto_h   "${_proto_dir}/${_short}.h")
    set(_lvo_h     "${_lvo_dir}/lvo.h")
    set(_vec_c     "${_vec_dir}/${_short}_vec.c")
    set(_aistr_inc "${_aistr_dir}/${_short}.inc")

    file(MAKE_DIRECTORY
        "${_proto_dir}"
        "${_lvo_dir}"
        "${_vec_dir}"
        "${_aistr_dir}"
    )

    set(_deps "${conf_path}")
    if(CARA_TARGET STREQUAL "host")
        list(APPEND _deps lvo-gen)
    endif()

    add_custom_command(
        OUTPUT  "${_proto_h}"
        COMMAND ${CARA_LVO_GEN_BIN} --emit proto "${conf_path}" "${_proto_h}"
        DEPENDS ${_deps}
        VERBATIM
        COMMENT "lvo-gen proto ${_libname}"
    )
    add_custom_command(
        OUTPUT  "${_lvo_h}"
        COMMAND ${CARA_LVO_GEN_BIN} --emit lvo "${conf_path}" "${_lvo_h}"
        DEPENDS ${_deps}
        VERBATIM
        COMMENT "lvo-gen lvo  ${_libname}"
    )
    add_custom_command(
        OUTPUT  "${_vec_c}"
        COMMAND ${CARA_LVO_GEN_BIN} --emit vec "${conf_path}" "${_vec_c}"
        DEPENDS ${_deps}
        VERBATIM
        COMMENT "lvo-gen vec  ${_libname}"
    )
    add_custom_command(
        OUTPUT  "${_aistr_inc}"
        COMMAND ${CARA_LVO_GEN_BIN} --emit aistreoir "${conf_path}" "${_aistr_inc}"
        DEPENDS ${_deps}
        VERBATIM
        COMMENT "lvo-gen aistr ${_libname}"
    )

    # Track per-conf outputs as a custom target so callers can attach
    # `add_dependencies(cara_<short>_lib_gen)` to their library targets.
    add_custom_target("cara_${_short}_lib_gen"
        DEPENDS "${_proto_h}" "${_lvo_h}" "${_vec_c}" "${_aistr_inc}"
    )

    # Push the aistreoir fragment onto the aggregator list (kept as a
    # CACHE INTERNAL variable so it survives across function scopes).
    set(_frags ${cara_lvo_gen_aistreoir_frags})
    list(APPEND _frags "${_aistr_inc}")
    set(cara_lvo_gen_aistreoir_frags "${_frags}" CACHE INTERNAL
        "Per-library aistreoir fragments queued for aggregation.")
endfunction()

# Aggregate all fragments registered so far into the final
# include/aistreoir/lvo_table.gen.h. Call once after every
# cara_lvo_gen_library() invocation.
function(cara_lvo_gen_aggregate)
    if(NOT CARA_LVO_GEN_BIN)
        return()
    endif()
    set(_out "${CARA_LVO_GEN_OUT_DIR}/include/aistreoir/lvo_table.gen.h")
    file(MAKE_DIRECTORY "${CARA_LVO_GEN_OUT_DIR}/include/aistreoir")

    if(NOT cara_lvo_gen_aistreoir_frags)
        # Nothing to aggregate yet; emit an empty stub so any downstream
        # `#include <aistreoir/lvo_table.gen.h>` doesn't error out.
        file(WRITE "${_out}"
            "// SPDX-License-Identifier: BSD-2-Clause\n"
            "// AUTOGENERATED stub — no .conf libraries registered yet.\n")
        return()
    endif()

    set(_deps ${cara_lvo_gen_aistreoir_frags})
    if(CARA_TARGET STREQUAL "host")
        list(APPEND _deps lvo-gen)
    endif()
    add_custom_command(
        OUTPUT  "${_out}"
        COMMAND ${CARA_LVO_GEN_BIN} --aggregate "${_out}" ${cara_lvo_gen_aistreoir_frags}
        DEPENDS ${_deps}
        VERBATIM
        COMMENT "lvo-gen aggregate (${_out})"
    )
    add_custom_target(cara_aistreoir_table DEPENDS "${_out}")
endfunction()
