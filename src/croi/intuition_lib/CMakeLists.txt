# SPDX-License-Identifier: BSD-2-Clause
#
# cara_intuition_lib: kernel-side V36+ intuition.library bridge.
#
# Tier I1 only runs lvo-gen against tools/lvo-gen/intuition.conf to emit
# the artefact families (docs/LVO.md §10.2):
#   ${CARA_LVO_GEN_OUT_DIR}/include/proto/intuition.h        — inline stubs
#   ${CARA_LVO_GEN_OUT_DIR}/include/intuition/lvo.h          — CARA_IDX_*/_LVO*
#   ${CARA_LVO_GEN_OUT_DIR}/src/croi/intuition_lib/intuition_vec.c
#   ${CARA_LVO_GEN_OUT_DIR}/aistreoir/intuition.inc          — Phase 9 fragment
#
# The trampolines (.lib_text.intuition), the Croi_*_Impl bridges onto the
# Leargas substrate, the dispatcher arms, and linking the generated
# vec table in arrive in Tier I2.
cara_lvo_gen_library("${CMAKE_SOURCE_DIR}/tools/lvo-gen/intuition.conf")
