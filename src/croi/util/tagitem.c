// SPDX-License-Identifier: BSD-2-Clause
//
// Kernel-internal TagItem walker. Mirrors V36+ utility.library
// GetTagData semantics (3rd Edition Includes & Autodocs RKM,
// utility/tagitem.h) over the same struct layout, but lives in the
// brand namespace because utility.library itself does not yet exist.

#include <cara/tagitem.h>
#include <cara/types.h>

IPTR Croi_GetTagData(const struct TagItem *list, Tag tag, IPTR default_data)
{
    while (list != nullptr) {
        switch (list->ti_Tag) {
        case TAG_END:
            return default_data;
        case TAG_IGNORE:
            list++;
            break;
        case TAG_MORE:
            list = (const struct TagItem *)(uptr)list->ti_Data;
            break;
        case TAG_SKIP:
            list += 1 + list->ti_Data;
            break;
        default:
            if (list->ti_Tag == tag) {
                return list->ti_Data;
            }
            list++;
            break;
        }
    }
    return default_data;
}
