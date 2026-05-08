// SPDX-License-Identifier: BSD-2-Clause
//
// Library name registry — the V36+ exec.library LibList. Every
// constructed library appends its lib_Node here so OpenLibrary can
// find it by name. v0 keeps a single global list; multi-hart and
// late-bind concerns are deferred.

#include <cara/list.h>
#include <cara/log.h>
#include <cara/types.h>
#include <exec/libraries.h>
#include <exec/lists.h>
#include <exec/nodes.h>

#include "library_registry.h"

#include <stdbool.h>

// Empty struct List; initialised lazily on first registration so
// callers don't depend on a Croi_LibList_Init() boot order step.
static struct List g_lib_list = {
    .lh_Head     = nullptr,
    .lh_Tail     = nullptr,
    .lh_TailPred = nullptr,
    .lh_Type     = NT_LIBRARY,
    .l_pad       = 0,
};
static bool g_lib_list_inited = false;

static void list_init_once(void)
{
    if (g_lib_list_inited) {
        return;
    }
    g_lib_list.lh_Head     = (struct Node *)&g_lib_list.lh_Tail;
    g_lib_list.lh_Tail     = nullptr;
    g_lib_list.lh_TailPred = (struct Node *)&g_lib_list;
    g_lib_list_inited = true;
}

static bool name_equal(const char *a, const char *b)
{
    if (!a || !b) {
        return false;
    }
    while (*a && *a == *b) {
        a++;
        b++;
    }
    return *a == 0 && *b == 0;
}

void Croi_RegisterLibrary(struct Library *lib)
{
    if (!lib) {
        return;
    }
    list_init_once();
    // V36+ AddTail: tailpred-sentinel idiom on struct List.
    struct Node *n = &lib->lib_Node;
    n->ln_Succ = (struct Node *)&g_lib_list.lh_Tail;
    n->ln_Pred = g_lib_list.lh_TailPred;
    g_lib_list.lh_TailPred->ln_Succ = n;
    g_lib_list.lh_TailPred = n;
}

struct Library *Croi_FindLibraryByName(const char *name)
{
    if (!name) {
        return nullptr;
    }
    list_init_once();
    for (struct Node *n = g_lib_list.lh_Head; n->ln_Succ; n = n->ln_Succ) {
        if (n->ln_Type != NT_LIBRARY) {
            continue;
        }
        if (name_equal(n->ln_Name, name)) {
            // n is the lib_Node; it's the first field of struct Library.
            return (struct Library *)n;
        }
    }
    return nullptr;
}

usize Croi_LibList_Count(void)
{
    list_init_once();
    usize n = 0;
    for (struct Node *p = g_lib_list.lh_Head; p->ln_Succ; p = p->ln_Succ) {
        n++;
    }
    return n;
}
