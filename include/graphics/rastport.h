// SPDX-License-Identifier: BSD-2-Clause
//
// AmigaOS V36+ <graphics/rastport.h> — drawing surface state.
// Phase 1 forward-declares RastPort; the full V36+ definition lands
// with Phase 3 graphics.library. struct Screen (intuition/screens.h)
// holds a `struct RastPort *` per the LB pragmatic deviation (see
// docs/PHASE1_LEARGAS.md).
//
// Source: AmigaOS RKM Includes & Autodocs 3rd Edition,
// graphics/rastport.h.

#ifndef GRAPHICS_RASTPORT_H
#define GRAPHICS_RASTPORT_H

#include <exec/types.h>

struct RastPort;

#endif // GRAPHICS_RASTPORT_H
