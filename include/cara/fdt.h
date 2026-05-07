// SPDX-License-Identifier: BSD-2-Clause
//
// Cara FDT (device-tree) parser. Read-only, allocation-free, host-buildable.
// Parses a binary DTB conforming to Devicetree Specification v0.4. See
// docs/DTS_PARSER.md for the design and scope.
//
// All offsets returned by this API are relative to the start of the
// structure block, not the start of the blob. They are opaque to callers;
// pass them back into other Fdt_ functions unchanged.
//
// All const char * returned by this API point into the blob and are valid
// for as long as the blob remains mapped. Callers must not free them.

#ifndef CARA_FDT_H
#define CARA_FDT_H

#include <cara/types.h>

struct Fdt {
    const u8 *blob;
    u32 totalsize;
    u32 off_struct;
    u32 size_struct;
    u32 off_strings;
    u32 size_strings;
    u32 off_rsvmap;
    u32 version;
    u32 boot_cpuid_phys;
};

// ---- Lifecycle --------------------------------------------------------------

[[nodiscard]] int Fdt_Open(struct Fdt *out, const void *blob);

// ---- Node lookup ------------------------------------------------------------

// Returns the structure-block offset of the root node (its BEGIN_NODE token).
[[nodiscard]] u32 Fdt_Root(const struct Fdt *fdt);

// Walk by full path, e.g. "/", "/chosen", "/cpus/cpu@0".
[[nodiscard]] int Fdt_ResolvePath(const struct Fdt *fdt, const char *path,
                                  u32 *node_off_out);

// Find next node whose `compatible` property contains `compat`.
// First call: pass *node_off_inout = 0. Subsequent calls keep the previous
// match in the inout slot to advance. Returns CARA_ENOTFOUND when no
// further match exists.
[[nodiscard]] int Fdt_FindByCompatible(const struct Fdt *fdt,
                                       const char *compat,
                                       u32 *node_off_inout);

// Iterate immediate children of `parent`. First call: cursor must be 0.
// On success writes the child offset into *node_off_out; the cursor is
// updated for the next call. Returns CARA_ENOTFOUND when the iteration is
// exhausted.
[[nodiscard]] int Fdt_ChildIter(const struct Fdt *fdt, u32 parent,
                                u32 *cursor_inout, u32 *node_off_out);

// Returns the node name (the name portion of "name@unit-addr"). Empty
// string for root.
[[nodiscard]] const char *Fdt_NodeName(const struct Fdt *fdt, u32 node);

// ---- Property accessors -----------------------------------------------------

[[nodiscard]] int Fdt_PropU32(const struct Fdt *fdt, u32 node, const char *name,
                              u32 *out);

[[nodiscard]] int Fdt_PropU64(const struct Fdt *fdt, u32 node, const char *name,
                              u64 *out);

// Decode `reg` cell `index`. Consults the parent node's #address-cells and
// #size-cells (default 2 and 1 if absent).
[[nodiscard]] int Fdt_PropReg(const struct Fdt *fdt, u32 node, u32 index,
                              u64 *base_out, u64 *size_out);

// Returns a pointer into the blob for the named string property, or
// nullptr if the property is missing or not a NUL-terminated string.
[[nodiscard]] const char *Fdt_PropStr(const struct Fdt *fdt, u32 node,
                                      const char *name);

// Iterate strings in a string-list property (e.g. `compatible`).
// First call: *cursor_inout = 0. Returns CARA_ENOTFOUND when exhausted.
[[nodiscard]] int Fdt_PropStrIter(const struct Fdt *fdt, u32 node,
                                  const char *name, u32 *cursor_inout,
                                  const char **str_out);

// Raw byte access — caller decides what the bytes mean.
[[nodiscard]] int Fdt_PropRaw(const struct Fdt *fdt, u32 node, const char *name,
                              const void **bytes_out, u32 *len_out);

[[nodiscard]] bool Fdt_NodeIsCompatible(const struct Fdt *fdt, u32 node,
                                        const char *compat);

// ---- Memory reservation block ----------------------------------------------

// Iterate /memreserve/ entries. First call: *cursor_inout = 0. Returns
// CARA_ENOTFOUND when the (0,0) terminator is reached.
[[nodiscard]] int Fdt_RsvIter(const struct Fdt *fdt, u32 *cursor_inout,
                              u64 *base_out, u64 *size_out);

#endif
