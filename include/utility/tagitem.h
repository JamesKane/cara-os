// SPDX-License-Identifier: BSD-2-Clause
//
// AmigaOS V36+ tagged-list mechanism (3rd Edition RKMs — utility.library
// is the canonical home, but the struct and tag constants are needed
// by callers that haven't opened utility.library yet, e.g. the
// kernel-internal Croi_MakeLibrary path that walks an array on the
// boot stack).
//
// A TagItem array is terminated by an entry whose ti_Tag is TAG_END.
// Special control tags:
//   TAG_IGNORE — entry is skipped
//   TAG_MORE   — ti_Data is a `struct TagItem *` to chain to
//   TAG_SKIP   — skip the next ti_Data items
//   TAG_USER   — caller-defined tag IDs start at this value
//
// The public utility.library walker LVOs (FindTagItem, NextTagItem,
// GetTagData, FilterTagChanges, …) arrive with Phase 3's
// utility.library coverage. Until then, kernel-internal code uses
// Croi_GetTagData (see include/cara/tagitem_internal.h) which is the
// same array walk under a brand-namespace name.

#ifndef UTILITY_TAGITEM_H
#define UTILITY_TAGITEM_H

#include <exec/types.h>

typedef ULONG Tag;

// Note: V36+ classic Amiga had ti_Data = ULONG (32-bit). CaraOS
// widens it to IPTR (pointer-sized) so tag values can carry SASOS
// pointers — see <exec/types.h> note on IPTR. V36+ source that
// stored small integers via ti_Data still compiles unchanged.
struct TagItem {
    Tag   ti_Tag;
    IPTR  ti_Data;
};

#define TAG_END     ((Tag)0)            // terminates a TagItem array
#define TAG_DONE    TAG_END             // V36+ synonym
#define TAG_IGNORE  ((Tag)1)            // ignore this entry
#define TAG_MORE    ((Tag)2)            // ti_Data is a struct TagItem *
#define TAG_SKIP    ((Tag)3)            // skip ti_Data more entries

#define TAG_USER    ((Tag)0x80000000UL) // first caller-defined tag value

#endif // UTILITY_TAGITEM_H
