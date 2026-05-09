// SPDX-License-Identifier: BSD-2-Clause
//
// AmigaOS V36+ <graphics/layers.h> + <graphics/clip.h> — overlapping
// drawing region tracking. Phase 1 forward-declares Layer and
// Layer_Info; full V36+ definitions land with Phase 3 / Phase 4 layer
// support. Phase 1 windows do not overlap meaningfully — Clar's
// drawers are tiled — so the layer-tracking bodies arrive when
// requested-window overlap actually needs to clip correctly.
//
// Source: AmigaOS RKM Includes & Autodocs 3rd Edition, graphics/clip.h
// and graphics/layers.h.

#ifndef GRAPHICS_LAYERS_H
#define GRAPHICS_LAYERS_H

#include <exec/types.h>

struct Layer;
struct Layer_Info;

#endif // GRAPHICS_LAYERS_H
