// SPDX-License-Identifier: BSD-2-Clause
//
// KERNEL_TEST(device_io): L6.1 — the exec device IO primitives + the
// CaraDevice registry (docs/CROI_DEVICES.md). Registers a synthetic echo
// device, OpenDevices it, drives CMD_WRITE / an unknown command through
// DoIO, and proves SendIO replies to the request's reply port (the
// synchronous v0 path).

#include <cara/device.h>
#include <cara/exec_lib.h>
#include <cara/leargas.h>
#include <cara/test.h>
#include <cara/types.h>
#include <devices/input.h>
#include <devices/inputevent.h>
#include <devices/timer.h>
#include <exec/interrupts.h>
#include <exec/io.h>
#include <exec/nodes.h>
#include <exec/ports.h>
#include <exec/types.h>

// Echo device: CMD_WRITE reports io_Length as io_Actual; CMD_READ reads
// nothing; anything else is IOERR_NOCMD.
static void echo_beginio(struct IORequest *io)
{
    struct IOStdReq *r = (struct IOStdReq *)io;
    switch (io->io_Command) {
    case CMD_WRITE:
        r->io_Actual = r->io_Length;
        io->io_Error = 0;
        break;
    case CMD_READ:
        r->io_Actual = 0;
        io->io_Error = 0;
        break;
    default:
        io->io_Error = IOERR_NOCMD;
        break;
    }
}

static struct CaraDevice g_echo_dev = { .name = "echo.device", .beginio = echo_beginio };

KERNEL_TEST(device_io)
{
    Croi_Device_Register(&g_echo_dev);
    TEST_ASSERT(ctx, Croi_Device_Find("echo.device") == &g_echo_dev, "registry find");
    TEST_ASSERT(ctx, Croi_Device_Find("nope.device") == nullptr, "registry miss");

    // OpenDevice binds the device into the IORequest.
    struct IOStdReq io = { 0 };
    LONG err = Croi_OpenDevice_Impl((STRPTR) "echo.device", 0, (struct IORequest *)&io, 0);
    TEST_ASSERT(ctx, err == 0 && io.io_Device != nullptr, "OpenDevice");

    // An unknown device fails + leaves io_Device unbound.
    struct IOStdReq io2 = { 0 };
    TEST_ASSERT(ctx,
                Croi_OpenDevice_Impl((STRPTR) "nope.device", 0, (struct IORequest *)&io2, 0) ==
                        IOERR_OPENFAIL &&
                    io2.io_Device == nullptr,
                "OpenDevice unknown fails");

    // DoIO CMD_WRITE → io_Actual == io_Length, no error.
    io.io_Command = CMD_WRITE;
    io.io_Length = 42;
    TEST_ASSERT(ctx, Croi_DoIO_Impl((struct IORequest *)&io) == 0 && io.io_Actual == 42,
                "DoIO CMD_WRITE");

    // An unsupported command → IOERR_NOCMD.
    io.io_Command = 0x1234;
    TEST_ASSERT(ctx, Croi_DoIO_Impl((struct IORequest *)&io) == IOERR_NOCMD, "DoIO unknown cmd");

    // SendIO replies to the reply port (synchronous v0); WaitIO returns
    // the error.
    struct MsgPort *rp = Croi_CreateMsgPort_Impl();
    TEST_ASSERT(ctx, rp != nullptr, "reply port");
    io.io_Command = CMD_WRITE;
    io.io_Length = 7;
    io.io_Actual = 0;
    io.io_Message.mn_Node.ln_Type = NT_MESSAGE;
    io.io_Message.mn_ReplyPort = rp;
    Croi_SendIO_Impl((struct IORequest *)&io);
    TEST_ASSERT(ctx, Croi_GetMsg_Impl(rp) == &io.io_Message && io.io_Actual == 7,
                "SendIO ran + replied");
    TEST_ASSERT(ctx, Croi_WaitIO_Impl((struct IORequest *)&io) == 0, "WaitIO");

    Croi_CloseDevice_Impl((struct IORequest *)&io);
    TEST_ASSERT(ctx, io.io_Device == nullptr, "CloseDevice unbound");
    Croi_DeleteMsgPort_Impl(rp);
}

// L6.2 — timer.device over Croi_Time.
KERNEL_TEST(timer_device)
{
    Croi_Timer_Init(); // idempotent (boot already registered it)

    struct timerequest tr = { 0 };
    LONG err = Croi_OpenDevice_Impl((STRPTR) "timer.device", UNIT_MICROHZ, (struct IORequest *)&tr,
                                    0);
    TEST_ASSERT(ctx, err == 0 && tr.tr_node.io_Device != nullptr, "OpenDevice timer.device");

    // TR_GETSYSTIME → a plausible, normalised time.
    tr.tr_node.io_Command = TR_GETSYSTIME;
    TEST_ASSERT(ctx, Croi_DoIO_Impl((struct IORequest *)&tr) == 0, "DoIO TR_GETSYSTIME");
    TEST_ASSERT(ctx, tr.tr_time.tv_micro < 1000000u, "micros normalised");
    u64 t0 = (u64)tr.tr_time.tv_secs * 1000000ull + tr.tr_time.tv_micro;

    // TR_ADDREQUEST → wait ~1ms, completes.
    tr.tr_node.io_Command = TR_ADDREQUEST;
    tr.tr_time.tv_secs = 0;
    tr.tr_time.tv_micro = 1000;
    TEST_ASSERT(ctx, Croi_DoIO_Impl((struct IORequest *)&tr) == 0, "DoIO TR_ADDREQUEST");

    // The clock advanced across the wait.
    tr.tr_node.io_Command = TR_GETSYSTIME;
    Croi_DoIO_Impl((struct IORequest *)&tr);
    u64 t1 = (u64)tr.tr_time.tv_secs * 1000000ull + tr.tr_time.tv_micro;
    TEST_ASSERT(ctx, t1 >= t0, "system time advanced after TR_ADDREQUEST");

    // An unsupported command is rejected.
    tr.tr_node.io_Command = 0x4321;
    TEST_ASSERT(ctx, Croi_DoIO_Impl((struct IORequest *)&tr) == IOERR_NOCMD, "unknown timer cmd");

    Croi_CloseDevice_Impl((struct IORequest *)&tr);
}

// L6.3 — console.device over the L3.6 console sink.
KERNEL_TEST(console_device)
{
    Croi_Console_Init(); // idempotent (boot already registered it)

    struct IOStdReq io = { 0 };
    LONG err = Croi_OpenDevice_Impl((STRPTR) "console.device", 0, (struct IORequest *)&io, 0);
    TEST_ASSERT(ctx, err == 0 && io.io_Device != nullptr, "OpenDevice console.device");

    // CMD_WRITE → io_Actual == io_Length (bytes reach the cout sink).
    static const char msg[] = "console.device CMD_WRITE\n";
    io.io_Command = CMD_WRITE;
    io.io_Data = (APTR)(uptr)msg;
    io.io_Length = (ULONG)(sizeof(msg) - 1);
    io.io_Actual = 0;
    TEST_ASSERT(ctx, Croi_DoIO_Impl((struct IORequest *)&io) == 0, "DoIO CMD_WRITE");
    TEST_ASSERT(ctx, io.io_Actual == sizeof(msg) - 1, "CMD_WRITE io_Actual");

    // CMD_READ → EOF stub (0 bytes, no error).
    io.io_Command = CMD_READ;
    io.io_Actual = 7;
    TEST_ASSERT(ctx, Croi_DoIO_Impl((struct IORequest *)&io) == 0 && io.io_Actual == 0,
                "CMD_READ EOF");

    // An unsupported command is rejected.
    io.io_Command = 0x4321;
    TEST_ASSERT(ctx, Croi_DoIO_Impl((struct IORequest *)&io) == IOERR_NOCMD, "unknown console cmd");

    Croi_CloseDevice_Impl((struct IORequest *)&io);
}

// L6.4 — input.device over the Leargas input ring.
KERNEL_TEST(input_device)
{
    Croi_Input_Init();          // idempotent (boot already registered it)
    (void)Leargas_Input_Init(); // ensure the ring exists; idempotent

    struct IOStdReq io = { 0 };
    LONG err = Croi_OpenDevice_Impl((STRPTR) "input.device", 0, (struct IORequest *)&io, 0);
    TEST_ASSERT(ctx, err == 0 && io.io_Device != nullptr, "OpenDevice input.device");

    // Drain anything already queued so our injected event is observable.
    struct LeargasInputEvent drain;
    while (Leargas_Input_Read(&drain)) {
    }

    // IND_WRITEEVENT injects a two-event chain into the ring.
    struct InputEvent ev2 = {
        .ie_Class = IECLASS_RAWKEY,
        .ie_Code = 0x42,
        .ie_NextEvent = nullptr,
    };
    struct InputEvent ev1 = {
        .ie_Class = IECLASS_RAWMOUSE,
        .ie_Code = IECODE_LBUTTON,
        .ie_NextEvent = &ev2,
    };
    ev1.ie_X = 3;
    ev1.ie_Y = -4;
    io.io_Command = IND_WRITEEVENT;
    io.io_Data = (APTR)&ev1;
    io.io_Actual = 0;
    TEST_ASSERT(ctx, Croi_DoIO_Impl((struct IORequest *)&io) == 0, "DoIO IND_WRITEEVENT");
    TEST_ASSERT(ctx, io.io_Actual == 2, "IND_WRITEEVENT posted 2 events");

    // The events land in the ring, in order, translated to the flat shape.
    struct LeargasInputEvent r1 = { 0 };
    struct LeargasInputEvent r2 = { 0 };
    TEST_ASSERT(ctx, Leargas_Input_Read(&r1), "ring has event 1");
    TEST_ASSERT(ctx, Leargas_Input_Read(&r2), "ring has event 2");
    TEST_ASSERT(ctx,
                r1.ie_class == IECLASS_RAWMOUSE && r1.ie_code == IECODE_LBUTTON && r1.ie_dx == 3 &&
                    r1.ie_dy == -4,
                "event 1 translated");
    TEST_ASSERT(ctx, r2.ie_class == IECLASS_RAWKEY && r2.ie_code == 0x42, "event 2 translated");

    // IND_ADDHANDLER / IND_REMHANDLER are accepted (recorded, not invoked).
    struct Interrupt handler = { 0 };
    io.io_Command = IND_ADDHANDLER;
    io.io_Data = (APTR)&handler;
    TEST_ASSERT(ctx, Croi_DoIO_Impl((struct IORequest *)&io) == 0, "IND_ADDHANDLER");
    io.io_Command = IND_REMHANDLER;
    TEST_ASSERT(ctx, Croi_DoIO_Impl((struct IORequest *)&io) == 0, "IND_REMHANDLER");

    // A tuning verb is a no-op; an unknown command is rejected.
    io.io_Command = IND_SETPERIOD;
    TEST_ASSERT(ctx, Croi_DoIO_Impl((struct IORequest *)&io) == 0, "IND_SETPERIOD no-op");
    io.io_Command = 0x4321;
    TEST_ASSERT(ctx, Croi_DoIO_Impl((struct IORequest *)&io) == IOERR_NOCMD, "unknown input cmd");

    Croi_CloseDevice_Impl((struct IORequest *)&io);
}
