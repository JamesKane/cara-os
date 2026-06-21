// SPDX-License-Identifier: BSD-2-Clause
//
// AmigaOS V36+ devices/timer.h — the public timer.device surface.
// Phase 1 only needs `struct timeval` (it's embedded in
// <devices/inputevent.h>'s `struct InputEvent`); the timer.device
// commands and unit numbers land with Phase 3.
//
// Source: AmigaOS RKM Devices 3rd Edition, devices/timer.h.

#ifndef DEVICES_TIMER_H
#define DEVICES_TIMER_H

#include <exec/io.h>
#include <exec/types.h>

// V36+ timeval is two 32-bit unsigned fields. tv_secs is seconds since
// some epoch the producer chose (timer.device uses system uptime),
// tv_micro is microseconds in the range [0, 999_999]. Producers MUST
// keep tv_micro normalised; consumers may assume that.
struct timeval {
    ULONG tv_secs;
    ULONG tv_micro;
};

// ---- timer.device (L6.2) -------------------------------------------------
//
// A timerequest is the IORequest the timer takes; tr_time is the duration
// (TR_ADDREQUEST) or the result (TR_GETSYSTIME). v0 maps all units to the
// one monotonic Croi_Time clock (docs/CROI_DEVICES.md §2.3).
struct timerequest {
    struct IORequest tr_node;
    struct timeval tr_time;
};

// Units (OpenDevice's unit arg). v0 treats them all as the monotonic clock.
#define UNIT_MICROHZ 0
#define UNIT_VBLANK 1
#define UNIT_ECLOCK 2
#define UNIT_WAITUNTIL 3
#define UNIT_WAITECLOCK 4

// Commands (io_Command). TR_* extend the standard CMD_* set.
#define TR_ADDREQUEST (CMD_NONSTD + 0) // wait tr_time, then complete
#define TR_GETSYSTIME (CMD_NONSTD + 1) // tr_time = current system time
#define TR_SETSYSTIME (CMD_NONSTD + 2) // set the clock (ignored in v0)

#endif // DEVICES_TIMER_H
