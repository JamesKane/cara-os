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
