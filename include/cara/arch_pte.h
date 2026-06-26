// SPDX-License-Identifier: BSD-2-Clause
//
// cara/arch_pte.h — the architecture's page-table-entry encoding (epic H), as a
// CARA_ARCH-selected dispatch header. The portable generic walk in
// src/croi/mm/pt.c and the map flags in cara/mm.h include this and get the
// right backend's pure-inline encoding (PTE_* flags + arch_pte_* ops). One ISA
// per build image, like the rest of the HAL (cara/arch.h).
//
// Selection: the kernel build defines CARA_ARCH_ARM64 for the AArch64 backend
// (src/CMakeLists.txt); everything else — the RISC-V kernel and the host unit
// tests that build cara_mm — falls through to the RISC-V Sv39 variant, which is
// the default so no host/rv64 flag churn is needed.

#ifndef CARA_ARCH_PTE_H
#define CARA_ARCH_PTE_H

#if defined(CARA_ARCH_ARM64)
#include <cara/arch/arm64/arch_pte.h>
#else
#include <cara/arch/riscv64/arch_pte.h>
#endif

#endif // CARA_ARCH_PTE_H
