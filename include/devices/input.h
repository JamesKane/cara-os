// SPDX-License-Identifier: BSD-2-Clause
//
// AmigaOS V36+ devices/input.h — the input.device command set every
// Release-2 program targets (`<devices/input.h>`). input.device is the
// merge point of the raw input streams (keyboard, mouse, timer) into
// one event chain that handlers (intuition last) consume.
//
// Command numbers are V36+ verbatim (devices/input.h, Release 2.04):
// the IND_* specials start at CMD_NONSTD. An IND_WRITEEVENT request
// carries a `struct InputEvent` chain in io_Data (io_Length bytes);
// IND_ADDHANDLER/IND_REMHANDLER carry a `struct Interrupt` handler in
// io_Data.
//
// CaraOS v0 (L6.4): IND_WRITEEVENT injects events into the Leargas
// input ring (Leargas drains it directly today); the handler-chain
// verbs are recorded but not yet invoked — the full chain is a later
// refactor (docs/CROI_DEVICES.md §2.5).
//
// Source: AmigaOS RKM Devices 3rd Edition §10 (Input Device) and the
// canonical <devices/input.h> in the V36+ Includes.

#ifndef DEVICES_INPUT_H
#define DEVICES_INPUT_H

#include <exec/io.h>

// input.device commands (io_Command). All are CMD_NONSTD specials.
#define IND_ADDHANDLER (CMD_NONSTD + 0) // io_Data = struct Interrupt *
#define IND_REMHANDLER (CMD_NONSTD + 1) // io_Data = struct Interrupt *
#define IND_WRITEEVENT (CMD_NONSTD + 2) // io_Data = struct InputEvent chain
#define IND_SETTHRESH (CMD_NONSTD + 3)  // key-repeat threshold
#define IND_SETPERIOD (CMD_NONSTD + 4)  // key-repeat period
#define IND_SETMPORT (CMD_NONSTD + 5)   // select mouse port
#define IND_SETMTYPE (CMD_NONSTD + 6)   // set mouse-port type
#define IND_SETMTRIG (CMD_NONSTD + 7)   // set mouse trigger conditions

#endif // DEVICES_INPUT_H
