// SPDX-License-Identifier: BSD-2-Clause
//
// V36+ utility.library tag-list walkers + Hook dispatcher — the
// `local`-flavour LVO impls. Like src/croi/exec_lib/list_ops.c these
// live in the .lib_text.utility RX page (mapped at user VA 0x4000_0000,
// LVO.md §5.1) and run in U-mode: the proto/utility.h inline stub JALRs
// straight here, no syscall. That imposes the same self-containment
// rule as list_ops.c — no kernel symbols, no globals, and no calls out
// of the section (a cross-section call would be an out-of-range
// R_RISCV_CALL_PLT). So the walkers each inline their own iteration
// rather than calling NextTagItem/FindTagItem.
//
// lvo-gen appends the trailing `struct UtilityBase *base` arg to every
// row; these impls ignore it (utility.library is base-less helper code).

#include <cara/types.h>
#include <exec/types.h>
#include <utility/hooks.h>
#include <utility/tagitem.h>

#define LIBTEXT_U __attribute__((section(".lib_text.utility"), used))

struct UtilityBase;

// FindTagItem — first TagItem in tagList whose ti_Tag == tagValue, or
// nullptr. Honours the TAG_IGNORE / TAG_MORE / TAG_SKIP control tags.
LIBTEXT_U struct TagItem *Croi_Utility_FindTagItem(Tag tagValue, struct TagItem *tagList,
                                                   struct UtilityBase *base)
{
    (void)base;
    struct TagItem *ti = tagList;
    while (ti) {
        Tag t = ti->ti_Tag;
        if (t == TAG_DONE) {
            return nullptr;
        } else if (t == TAG_IGNORE) {
            ti++;
        } else if (t == TAG_MORE) {
            ti = (struct TagItem *)ti->ti_Data;
        } else if (t == TAG_SKIP) {
            ti += ti->ti_Data + 1;
        } else if (t == tagValue) {
            return ti;
        } else {
            ti++;
        }
    }
    return nullptr;
}

// GetTagData — ti_Data of the matching TagItem, else defaultVal.
LIBTEXT_U IPTR Croi_Utility_GetTagData(Tag tagValue, IPTR defaultVal, struct TagItem *tagList,
                                       struct UtilityBase *base)
{
    (void)base;
    struct TagItem *ti = tagList;
    while (ti) {
        Tag t = ti->ti_Tag;
        if (t == TAG_DONE) {
            return defaultVal;
        } else if (t == TAG_IGNORE) {
            ti++;
        } else if (t == TAG_MORE) {
            ti = (struct TagItem *)ti->ti_Data;
        } else if (t == TAG_SKIP) {
            ti += ti->ti_Data + 1;
        } else if (t == tagValue) {
            return ti->ti_Data;
        } else {
            ti++;
        }
    }
    return defaultVal;
}

// NextTagItem — iterator. *tagItemPtr points at the cursor; returns the
// next "real" TagItem and advances the cursor past it, or nullptr at the
// end (also nulling the cursor). Resolves TAG_MORE chains transparently.
LIBTEXT_U struct TagItem *Croi_Utility_NextTagItem(struct TagItem **tagItemPtr,
                                                   struct UtilityBase *base)
{
    (void)base;
    if (!tagItemPtr) {
        return nullptr;
    }
    struct TagItem *ti = *tagItemPtr;
    while (ti) {
        Tag t = ti->ti_Tag;
        if (t == TAG_DONE) {
            break;
        } else if (t == TAG_IGNORE) {
            ti++;
        } else if (t == TAG_MORE) {
            ti = (struct TagItem *)ti->ti_Data;
        } else if (t == TAG_SKIP) {
            ti += ti->ti_Data + 1;
        } else {
            *tagItemPtr = ti + 1;
            return ti;
        }
    }
    *tagItemPtr = nullptr;
    return nullptr;
}

// PackBoolTags — fold a set of boolean TagItems into a flag word.
// boolMap maps tag ID → flag bit; for each tag present in tagList that
// boolMap knows, set the bit if ti_Data is non-zero, else clear it.
LIBTEXT_U ULONG Croi_Utility_PackBoolTags(ULONG initialFlags, struct TagItem *tagList,
                                          struct TagItem *boolMap, struct UtilityBase *base)
{
    (void)base;
    ULONG flags = initialFlags;
    struct TagItem *ti = tagList;
    while (ti) {
        Tag t = ti->ti_Tag;
        if (t == TAG_DONE) {
            break;
        } else if (t == TAG_IGNORE) {
            ti++;
            continue;
        } else if (t == TAG_MORE) {
            ti = (struct TagItem *)ti->ti_Data;
            continue;
        } else if (t == TAG_SKIP) {
            ti += ti->ti_Data + 1;
            continue;
        }
        // Real tag: look it up in the (flat) boolMap.
        ULONG bit = 0;
        for (struct TagItem *m = boolMap; m && m->ti_Tag != TAG_DONE; m++) {
            if (m->ti_Tag == t) {
                bit = (ULONG)m->ti_Data;
                break;
            }
        }
        if (bit) {
            if (ti->ti_Data) {
                flags |= bit;
            } else {
                flags &= ~bit;
            }
        }
        ti++;
    }
    return flags;
}

// TagInArray — TRUE if tagValue appears in the TAG_END-terminated array.
LIBTEXT_U BOOL Croi_Utility_TagInArray(Tag tagValue, Tag *tagArray, struct UtilityBase *base)
{
    (void)base;
    if (!tagArray) {
        return FALSE;
    }
    for (Tag *p = tagArray; *p != TAG_DONE; p++) {
        if (*p == tagValue) {
            return TRUE;
        }
    }
    return FALSE;
}

// MapTags — remap the ti_Tag of each real entry in tagList using
// mapList (a flat oldTag→newTag map: ti_Tag = match, ti_Data = new tag).
// Entries not in mapList become TAG_IGNORE (MAP_REMOVE_NOT_FOUND) or are
// left untouched (MAP_KEEP_NOT_FOUND). In-place.
LIBTEXT_U void Croi_Utility_MapTags(struct TagItem *tagList, struct TagItem *mapList, ULONG mapType,
                                    struct UtilityBase *base)
{
    (void)base;
    struct TagItem *ti = tagList;
    while (ti) {
        Tag t = ti->ti_Tag;
        if (t == TAG_DONE) {
            break;
        } else if (t == TAG_IGNORE) {
            ti++;
            continue;
        } else if (t == TAG_MORE) {
            ti = (struct TagItem *)ti->ti_Data;
            continue;
        } else if (t == TAG_SKIP) {
            ti += ti->ti_Data + 1;
            continue;
        }
        bool found = false;
        for (struct TagItem *m = mapList; m && m->ti_Tag != TAG_DONE; m++) {
            if (m->ti_Tag == t) {
                ti->ti_Tag = (Tag)m->ti_Data;
                found = true;
                break;
            }
        }
        if (!found && mapType == MAP_REMOVE_NOT_FOUND) {
            ti->ti_Tag = TAG_IGNORE;
        }
        ti++;
    }
}

// FilterTagItems — keep only the entries of a flat tagList whose tag is
// (AND) / is not (NOT) present in filterArray, compacting in place and
// re-terminating with TAG_DONE. Returns the number of entries kept.
// Operates on a flat (unchained) list — the common usage; TAG_IGNORE
// entries are dropped.
LIBTEXT_U ULONG Croi_Utility_FilterTagItems(struct TagItem *tagList, Tag *filterArray, ULONG logic,
                                            struct UtilityBase *base)
{
    (void)base;
    if (!tagList) {
        return 0;
    }
    struct TagItem *rd = tagList;
    struct TagItem *wr = tagList;
    ULONG count = 0;
    while (rd->ti_Tag != TAG_DONE) {
        Tag t = rd->ti_Tag;
        if (t != TAG_IGNORE) {
            bool in = false;
            for (Tag *f = filterArray; f && *f != TAG_DONE; f++) {
                if (*f == t) {
                    in = true;
                    break;
                }
            }
            bool keep = (logic == TAGFILTER_AND) ? in : !in;
            if (keep) {
                if (wr != rd) {
                    *wr = *rd;
                }
                wr++;
                count++;
            }
        }
        rd++;
    }
    wr->ti_Tag = TAG_DONE;
    wr->ti_Data = 0;
    return count;
}

// FilterTagChanges — drop entries of changeList whose value already
// matches the same tag in originalList (no real change), compacting in
// place. A tag absent from originalList counts as a change (kept). If
// apply is TRUE, the surviving new values are written back into
// originalList. Flat-list operation.
LIBTEXT_U void Croi_Utility_FilterTagChanges(struct TagItem *changeList,
                                             struct TagItem *originalList, BOOL apply,
                                             struct UtilityBase *base)
{
    (void)base;
    if (!changeList) {
        return;
    }
    struct TagItem *rd = changeList;
    struct TagItem *wr = changeList;
    while (rd->ti_Tag != TAG_DONE) {
        Tag t = rd->ti_Tag;
        if (t == TAG_IGNORE) {
            rd++;
            continue;
        }
        struct TagItem *orig = nullptr;
        for (struct TagItem *o = originalList; o && o->ti_Tag != TAG_DONE; o++) {
            if (o->ti_Tag == t) {
                orig = o;
                break;
            }
        }
        bool changed = (orig == nullptr) || (orig->ti_Data != rd->ti_Data);
        if (changed) {
            if (apply && orig) {
                orig->ti_Data = rd->ti_Data;
            }
            if (wr != rd) {
                *wr = *rd;
            }
            wr++;
        }
        rd++;
    }
    wr->ti_Tag = TAG_DONE;
    wr->ti_Data = 0;
}

// CallHookPkt — invoke a Hook: hook->h_Entry(hook, object, paramPacket).
LIBTEXT_U ULONG Croi_Utility_CallHookPkt(struct Hook *hook, APTR object, APTR paramPacket,
                                         struct UtilityBase *base)
{
    (void)base;
    if (!hook || !hook->h_Entry) {
        return 0;
    }
    return hook->h_Entry(hook, object, paramPacket);
}
