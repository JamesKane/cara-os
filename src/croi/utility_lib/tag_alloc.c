// SPDX-License-Identifier: BSD-2-Clause
//
// V36+ utility.library allocating tag helpers — the `syscall`-flavour
// LVO impls. Unlike the walkers in tag_ops.c (pure, U-mode, in the RX
// page), these allocate, so they run in the kernel: the trampolines in
// trampolines.S ecall here via src/croi/syscall/syscall.c. Memory comes
// from the SASOS shared heap through Croi_AllocVec_Impl (the AllocVec
// size-header lets FreeTagItems free by pointer alone), so the returned
// tag lists are readable + writable from U-mode.

#include <cara/exec_lib.h>
#include <cara/types.h>
#include <cara/utility_lib.h>
#include <exec/memory.h>
#include <exec/types.h>
#include <utility/tagitem.h>

// Kernel-side NextTagItem: advance past control tags, resolve TAG_MORE,
// return the next real item or nullptr at the end. (We can't call the
// U-mode .lib_text.utility impl from S-mode, so this is a private copy.)
static struct TagItem *u_next(struct TagItem **pp)
{
    struct TagItem *ti = *pp;
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
            *pp = ti + 1;
            return ti;
        }
    }
    *pp = nullptr;
    return nullptr;
}

struct TagItem *Croi_Utility_AllocateTagItems_Impl(ULONG numTags)
{
    if (numTags == 0) {
        return nullptr;
    }
    // MEMF_CLEAR → every slot reads as TAG_DONE (TAG_END == 0) until the
    // caller fills it.
    return (struct TagItem *)Croi_AllocVec_Impl(numTags * (ULONG)sizeof(struct TagItem),
                                                MEMF_CLEAR);
}

struct TagItem *Croi_Utility_CloneTagItems_Impl(struct TagItem *original)
{
    u32 n = 0;
    struct TagItem *cur = original;
    while (u_next(&cur)) {
        n++;
    }

    struct TagItem *out =
        (struct TagItem *)Croi_AllocVec_Impl((n + 1) * (ULONG)sizeof(struct TagItem), MEMF_CLEAR);
    if (!out) {
        return nullptr;
    }

    cur = original;
    u32 i = 0;
    struct TagItem *t;
    while ((t = u_next(&cur)) != nullptr) {
        out[i].ti_Tag = t->ti_Tag;
        out[i].ti_Data = t->ti_Data;
        i++;
    }
    out[i].ti_Tag = TAG_DONE;
    out[i].ti_Data = 0;
    return out;
}

void Croi_Utility_FreeTagItems_Impl(struct TagItem *tagList)
{
    if (tagList) {
        Croi_FreeVec_Impl((APTR)tagList);
    }
}

void Croi_Utility_RefreshTagItemClones_Impl(struct TagItem *clone, struct TagItem *original)
{
    if (!clone || !original) {
        return;
    }
    struct TagItem *cur = original;
    u32 i = 0;
    struct TagItem *t;
    while ((t = u_next(&cur)) != nullptr) {
        clone[i].ti_Tag = t->ti_Tag;
        clone[i].ti_Data = t->ti_Data;
        i++;
    }
    clone[i].ti_Tag = TAG_DONE;
    clone[i].ti_Data = 0;
}
