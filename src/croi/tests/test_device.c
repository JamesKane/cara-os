// SPDX-License-Identifier: BSD-2-Clause
//
// KERNEL_TEST(device_io): L6.1 — the exec device IO primitives + the
// CaraDevice registry (docs/CROI_DEVICES.md). Registers a synthetic echo
// device, OpenDevices it, drives CMD_WRITE / an unknown command through
// DoIO, and proves SendIO replies to the request's reply port (the
// synchronous v0 path).

#include <cara/device.h>
#include <cara/exec_lib.h>
#include <cara/test.h>
#include <cara/types.h>
#include <devices/timer.h>
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
