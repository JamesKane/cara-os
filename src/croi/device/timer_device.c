// SPDX-License-Identifier: BSD-2-Clause
//
// timer.device (L6.2, docs/CROI_DEVICES.md §2.3) — over Croi_Time.
// TR_GETSYSTIME reads the monotonic clock; TR_ADDREQUEST waits a duration
// (poll-yield over Croi_Time, like Delay); TR_SETSYSTIME is accepted but
// ignored (the clock isn't settable in v0). All units map to the one
// monotonic clock. Registered with the device registry at boot.

#include <cara/device.h>
#include <cara/sched.h> // Croi_Yield
#include <cara/time.h>  // Croi_Time_Now
#include <cara/types.h>
#include <devices/timer.h>
#include <exec/io.h>

static void timer_getsystime(struct timeval *tv)
{
    u64 ns = Croi_Time_Now();
    tv->tv_secs = (ULONG)(ns / 1000000000ull);
    tv->tv_micro = (ULONG)((ns % 1000000000ull) / 1000ull);
}

static void timer_beginio(struct IORequest *io)
{
    struct timerequest *tr = (struct timerequest *)io;
    switch (io->io_Command) {
    case TR_GETSYSTIME:
        timer_getsystime(&tr->tr_time);
        io->io_Error = 0;
        break;
    case TR_SETSYSTIME:
        io->io_Error = 0; // monotonic clock not settable in v0
        break;
    case TR_ADDREQUEST: {
        // tr_time is a duration; wait it out over Croi_Time (cooperative
        // poll-yield, like Delay — the input pump / other tasks run).
        u64 dur_ns = (u64)tr->tr_time.tv_secs * 1000000000ull + (u64)tr->tr_time.tv_micro * 1000ull;
        u64 deadline = Croi_Time_Now() + dur_ns;
        while (Croi_Time_Now() < deadline) {
            Croi_Yield();
        }
        io->io_Error = 0;
        break;
    }
    case CMD_CLEAR:
        io->io_Error = 0;
        break;
    default:
        io->io_Error = IOERR_NOCMD;
        break;
    }
}

static struct CaraDevice g_timer_device = {
    .name = "timer.device",
    .beginio = timer_beginio,
};

void Croi_Timer_Init(void)
{
    Croi_Device_Register(&g_timer_device);
}
