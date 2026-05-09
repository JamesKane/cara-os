// SPDX-License-Identifier: BSD-2-Clause
//
// AmigaOS V36+ <intuition/intuition.h> — the public window-system API.
// Phase 1 forward-declares Window / Gadget / Requester / Menu so
// other intuition headers (intuition/screens.h) can reference them
// by pointer; the full structs land with epics LD (windows),
// LG (gadgets), and Phase 3 (requesters / menus).
//
// Source: AmigaOS RKM Includes & Autodocs 3rd Edition,
// intuition/intuition.h.

#ifndef INTUITION_INTUITION_H
#define INTUITION_INTUITION_H

#include <exec/types.h>

struct Window;
struct Gadget;
struct Requester;
struct Menu;

#endif // INTUITION_INTUITION_H
