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

#include <exec/types.h>

// V36+ timeval is two 32-bit unsigned fields. tv_secs is seconds since
// some epoch the producer chose (timer.device uses system uptime),
// tv_micro is microseconds in the range [0, 999_999]. Producers MUST
// keep tv_micro normalised; consumers may assume that.
struct timeval {
    ULONG tv_secs;
    ULONG tv_micro;
};

#endif // DEVICES_TIMER_H
