// SPDX-License-Identifier: BSD-2-Clause
//
// Kernel-side impl prototypes for the `syscall`-flavour utility.library
// LVOs — the allocating tag helpers. (utility.library's tag-list
// walkers + Hook dispatcher are `local` flavour and run entirely in the
// .lib_text.utility RX page; they never reach the kernel, so they have
// no prototypes here.)
//
// Each impl below is what src/croi/syscall/syscall.c routes to when the
// matching SYS_* trap arrives via the per-LVO trampoline
// (Cara_Trampoline_<Name> in src/croi/utility_lib/trampolines.S). As
// with exec, the signature is the V36+ canonical signature minus the
// trailing base pointer.

#ifndef CARA_UTILITY_LIB_H
#define CARA_UTILITY_LIB_H

#include <cara/types.h>
#include <utility/tagitem.h>

// AllocateTagItems — a cleared tag list of `numTags` items (the caller
// terminates it with TAG_DONE). Backed by the AllocVec size-header so
// FreeTagItems needs only the pointer. nullptr on failure / numTags 0.
struct TagItem *Croi_Utility_AllocateTagItems_Impl(ULONG numTags);

// CloneTagItems — a flat duplicate of `original` (TAG_MORE chains
// resolved, control tags dropped), TAG_DONE-terminated. nullptr on
// failure. Free with FreeTagItems.
struct TagItem *Croi_Utility_CloneTagItems_Impl(struct TagItem *original);

// FreeTagItems — release a list from AllocateTagItems / CloneTagItems.
void Croi_Utility_FreeTagItems_Impl(struct TagItem *tagList);

// RefreshTagItemClones — restore `clone` (made from `original`) so its
// values match `original` again.
void Croi_Utility_RefreshTagItemClones_Impl(struct TagItem *clone, struct TagItem *original);

#endif // CARA_UTILITY_LIB_H
