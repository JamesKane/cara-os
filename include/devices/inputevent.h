// SPDX-License-Identifier: BSD-2-Clause
//
// AmigaOS V36+ devices/inputevent.h — the canonical InputEvent shape
// every Release-2 program targets (`<devices/inputevent.h>`). The
// kernel-internal Phase 1 input ring (`<cara/leargas.h>`) carries a
// flat brand-namespace event; this header is the V36+ record shape
// that input.device translates into when Phase 3 publishes the LVO
// surface. Both shapes carry the same logical payload.
//
// IECLASS / IECODE / IEQUALIFIER / IESUBCLASS values match V36+
// verbatim (devices/inputevent.h, Release 2.04, Revision 36.7).
//
// Source: AmigaOS RKM Devices 3rd Edition, §10 (Input Device) and
// the canonical <devices/inputevent.h> in the V36+ Includes.

#ifndef DEVICES_INPUTEVENT_H
#define DEVICES_INPUTEVENT_H

#include <devices/timer.h>
#include <exec/types.h>

// ---- ie_Class --------------------------------------------------------------

enum {
    IECLASS_NULL = 0x00,
    IECLASS_RAWKEY = 0x01,
    IECLASS_RAWMOUSE = 0x02,
    IECLASS_EVENT = 0x03,
    IECLASS_POINTERPOS = 0x04,
    IECLASS_TIMER = 0x06,
    IECLASS_GADGETDOWN = 0x07,
    IECLASS_GADGETUP = 0x08,
    IECLASS_REQUESTER = 0x09,
    IECLASS_MENULIST = 0x0A,
    IECLASS_CLOSEWINDOW = 0x0B,
    IECLASS_SIZEWINDOW = 0x0C,
    IECLASS_REFRESHWINDOW = 0x0D,
    IECLASS_NEWPREFS = 0x0E,
    IECLASS_DISKREMOVED = 0x0F,
    IECLASS_DISKINSERTED = 0x10,
    IECLASS_ACTIVEWINDOW = 0x11,
    IECLASS_INACTIVEWINDOW = 0x12,
    IECLASS_NEWPOINTERPOS = 0x13, // V36+
    IECLASS_MENUHELP = 0x14,      // V36+
    IECLASS_CHANGEWINDOW = 0x15,  // V36+
    IECLASS_MAX = IECLASS_CHANGEWINDOW,
};

// ---- ie_SubClass (used with IECLASS_NEWPOINTERPOS) -------------------------

enum {
    IESUBCLASS_COMPATIBLE = 0x00,
    IESUBCLASS_PIXEL = 0x01,
    IESUBCLASS_TABLET = 0x02,
};

// ---- ie_Code ---------------------------------------------------------------
//
// IECLASS_RAWKEY: ie_Code carries the V36+ rawkey number. Bit 7
// (IECODE_UP_PREFIX) flags the up-stroke (key release).
//
// IECLASS_RAWMOUSE: ie_Code carries one of the IECODE_*BUTTON
// transitions; mouse-move events use IECODE_NOBUTTON and rely on
// ie_X / ie_Y deltas.

enum : u8 {
    IECODE_UP_PREFIX = 0x80,

    IECODE_LBUTTON = 0x68,
    IECODE_RBUTTON = 0x69,
    IECODE_MBUTTON = 0x6A,
    IECODE_NOBUTTON = 0xFF,

    // IECLASS_EVENT (V36+) sub-codes used by intuition for active /
    // resize / refresh broadcasts. Listed for completeness; Phase 1
    // only emits BUTTON/NOBUTTON values from the HID side.
    IECODE_NEWACTIVE = 0x01,
    IECODE_NEWSIZE = 0x02,
    IECODE_REFRESH = 0x03,
};

// ---- ie_Qualifier (canonical V36+ bit assignments) ------------------------

enum : u16 {
    IEQUALIFIER_LSHIFT = 0x0001,
    IEQUALIFIER_RSHIFT = 0x0002,
    IEQUALIFIER_CAPSLOCK = 0x0004,
    IEQUALIFIER_CONTROL = 0x0008,
    IEQUALIFIER_LALT = 0x0010,
    IEQUALIFIER_RALT = 0x0020,
    IEQUALIFIER_LCOMMAND = 0x0040, // left Amiga / Win / GUI
    IEQUALIFIER_RCOMMAND = 0x0080, // right Amiga / Win / GUI
    IEQUALIFIER_NUMERICPAD = 0x0100,
    IEQUALIFIER_REPEAT = 0x0200,
    IEQUALIFIER_INTERRUPT = 0x0400,
    IEQUALIFIER_MULTIBROADCAST = 0x0800,
    IEQUALIFIER_MIDBUTTON = 0x1000,
    IEQUALIFIER_RBUTTON = 0x2000,
    IEQUALIFIER_LEFTBUTTON = 0x4000,
    IEQUALIFIER_RELATIVEMOUSE = 0x8000,
};

// ---- struct InputEvent ----------------------------------------------------

struct InputEvent {
    struct InputEvent *ie_NextEvent;
    UBYTE ie_Class;
    UBYTE ie_SubClass;
    UWORD ie_Code;
    UWORD ie_Qualifier;
    union {
        struct {
            WORD ie_x;
            WORD ie_y;
        } ie_xy;
        APTR ie_addr;
    } ie_position;
    struct timeval ie_TimeStamp;
};

// V36+ keeps this set of accessor macros for the position union;
// they're spelled exactly the same here so V36+ source compiles
// without modification.
#define ie_X ie_position.ie_xy.ie_x
#define ie_Y ie_position.ie_xy.ie_y
#define ie_EventAddress ie_position.ie_addr

#endif // DEVICES_INPUTEVENT_H
