// SPDX-License-Identifier: BSD-2-Clause
//
// AmigaOS V36+ <graphics/view.h> — display surface description.
// Phase 1 forward-declares ViewPort and ColorMap; the full V36+
// definitions land with Phase 3 graphics.library. struct Screen
// (intuition/screens.h) holds a `struct ViewPort *` per the LB
// pragmatic deviation (see docs/PHASE1_LEARGAS.md).
//
// Source: AmigaOS RKM Includes & Autodocs 3rd Edition, graphics/view.h.

#ifndef GRAPHICS_VIEW_H
#define GRAPHICS_VIEW_H

#include <exec/types.h>

struct ViewPort;
struct ColorMap;

#endif // GRAPHICS_VIEW_H
