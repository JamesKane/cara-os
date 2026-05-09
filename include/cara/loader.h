// SPDX-License-Identifier: BSD-2-Clause
//
// Cara ELF loader. Walks PT_LOAD entries of a static RISC-V ELF64
// blob, allocates pages, copies the [p_offset, p_offset + p_filesz)
// file bytes plus a zero tail to p_memsz, and Page_Maps each page
// into the supplied user PT with permissions derived from p_flags.
//
// Only static ET_EXEC binaries are supported. Dynamic linking,
// PT_INTERP, relocations beyond what the linker has already applied,
// and PT_GNU_STACK / PT_TLS are out of scope.

#ifndef CARA_LOADER_H
#define CARA_LOADER_H

#include <cara/types.h>

struct PageTable;

// Validate the ELF header, walk PT_LOAD segments, and lay them down
// in `pt`. On success writes e_entry to *entry_va_out.
//
// Returns CARA_EOK on success or a negative error:
//   CARA_EBADMAGIC   - "\x7fELF" header missing
//   CARA_EBADVERSION - not ELF64 / not little-endian
//   CARA_EINVAL      - not ET_EXEC, wrong machine, malformed phdrs
//   CARA_ERANGE      - phdr / segment slice extends past blob bounds
//   CARA_ENOMEM      - page allocation or PT walk failed
[[nodiscard]] int Croi_LoadElf(const void *blob, usize size, struct PageTable *pt,
                               u64 *entry_va_out);

#endif
