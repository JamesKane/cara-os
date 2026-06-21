// SPDX-License-Identifier: BSD-2-Clause
//
// input.device (L6.4, docs/CROI_DEVICES.md §2.5) — minimal v0 over the
// Leargas input ring. IND_WRITEEVENT walks the struct InputEvent chain
// in io_Data, translates each to a LeargasInputEvent, and posts it into
// the ring (Leargas_Input_Post) — the same stream the HID pump feeds,
// which the Leargas router drains directly. The handler-chain verbs
// (IND_ADDHANDLER/IND_REMHANDLER) are RECORDED but NOT invoked: Leargas
// owns the ring today, so a real handler chain (handlers first,
// intuition last) is a later refactor. The IND_SET* tuning verbs are
// accepted as no-ops. Registered with the device registry at boot.

#include <cara/device.h>
#include <cara/leargas.h> // Leargas_Input_Post, struct LeargasInputEvent
#include <cara/log.h>
#include <cara/time.h> // Croi_Time_Now
#include <cara/types.h>
#include <devices/input.h>
#include <devices/inputevent.h>
#include <exec/interrupts.h>
#include <exec/io.h>

// v0 records the most-recently-added handler so the seam is visible;
// the chain is never invoked (Leargas drains the ring directly).
static struct Interrupt *g_input_handler;

// Translate one V36 InputEvent into the flat Leargas ring event and
// post it. Returns true if the ring accepted it.
static bool post_event(const struct InputEvent *ev)
{
    struct LeargasInputEvent le = {
        .ie_class = ev->ie_Class,
        .ie_subclass = ev->ie_SubClass,
        .ie_code = ev->ie_Code,
        .ie_qualifier = ev->ie_Qualifier,
        .ie_dx = ev->ie_X,
        .ie_dy = ev->ie_Y,
        .ie_ts_ns = Croi_Time_Now(),
    };
    return Leargas_Input_Post(&le);
}

static void input_beginio(struct IORequest *io)
{
    struct IOStdReq *r = (struct IOStdReq *)io;
    switch (io->io_Command) {
    case IND_WRITEEVENT: {
        // Walk the InputEvent chain (ie_NextEvent); io_Actual counts
        // the events that reached the ring.
        ULONG posted = 0;
        for (const struct InputEvent *ev = (const struct InputEvent *)r->io_Data; ev;
             ev = ev->ie_NextEvent) {
            if (post_event(ev)) {
                posted++;
            }
        }
        r->io_Actual = posted;
        io->io_Error = 0;
        break;
    }
    case IND_ADDHANDLER:
        g_input_handler = (struct Interrupt *)r->io_Data;
        Croi_Log(LOG_LV_INFO, "input", "IND_ADDHANDLER recorded (chain not invoked in v0)");
        io->io_Error = 0;
        break;
    case IND_REMHANDLER:
        if (g_input_handler == (struct Interrupt *)r->io_Data) {
            g_input_handler = nullptr;
        }
        io->io_Error = 0;
        break;
    case IND_SETTHRESH:
    case IND_SETPERIOD:
    case IND_SETMPORT:
    case IND_SETMTYPE:
    case IND_SETMTRIG:
        io->io_Error = 0; // tuning verbs accepted, no-op in v0
        break;
    default:
        io->io_Error = IOERR_NOCMD;
        break;
    }
}

static struct CaraDevice g_input_device = {
    .name = "input.device",
    .beginio = input_beginio,
};

void Croi_Input_Init(void)
{
    Croi_Device_Register(&g_input_device);
}
