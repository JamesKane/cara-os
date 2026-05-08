// SPDX-License-Identifier: BSD-2-Clause
//
// Kernel-internal TagItem walker.
//
// Phase A only: Croi_MakeLibrary (Phase C) takes a tagged-list of
// construction parameters; the kernel needs a minimal walker before
// utility.library exists. The public utility.library GetTagData /
// FindTagItem / NextTagItem LVOs ship with Phase 3 utility coverage
// and reuse this same walk over the same struct TagItem layout
// (defined in <utility/tagitem.h>).

#ifndef CARA_TAGITEM_H
#define CARA_TAGITEM_H

#include <utility/tagitem.h>

// Walk `list` (terminated by TAG_END or NULL) looking for `tag`.
// Honours TAG_IGNORE (skip), TAG_MORE (chain to ti_Data as TagItem *),
// TAG_SKIP (skip ti_Data more entries). Returns ti_Data of the first
// matching entry, or `default_data` if not found. Both return and
// default are IPTR so callers carrying pointer-sized data (e.g.
// MKL_VEC_TABLE) round-trip cleanly on RV64.
IPTR Croi_GetTagData(const struct TagItem *list, Tag tag, IPTR default_data);

#endif
