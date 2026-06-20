// SPDX-License-Identifier: BSD-2-Clause
//
// AmigaOS V36+ utility.library base struct (3rd Edition RKMs). Like
// every library base, the first field is a struct Library so the
// negative-side vec table sits at the canonical offset. The application
// global `UtilityBase` (declared in <proto/utility.h>) points here; the
// inline LVO stubs index the vec table off it.

#ifndef UTILITY_UTILITYBASE_H
#define UTILITY_UTILITYBASE_H

#include <exec/libraries.h>
#include <exec/types.h>

struct UtilityBase {
    struct Library ub_LibNode;
    UBYTE ub_Language;
    UBYTE ub_Reserved;
};

#endif // UTILITY_UTILITYBASE_H
