// SPDX-License-Identifier: BSD-2-Clause
//
// workbench/icon.h — icon.library's base type. The function prototypes
// live in the generated <proto/icon.h>; the LVO constants in the
// generated <icon/lvo.h>; the DiskObject ABI in <workbench/workbench.h>.
// IconBase carries no public fields beyond the LibNode (the CaraOS
// convention for these generated libraries, e.g. struct IFFParseBase).

#ifndef CARA_WORKBENCH_ICON_H
#define CARA_WORKBENCH_ICON_H

#include <exec/libraries.h>
#include <workbench/workbench.h>

struct IconBase {
    struct Library LibNode;
};

#endif // CARA_WORKBENCH_ICON_H
