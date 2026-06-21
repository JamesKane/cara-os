// SPDX-License-Identifier: BSD-2-Clause
//
// AmigaOS V36+ <exec/io.h> — the device IO model (L6). A device request
// is a struct IORequest the caller fills with an io_Command and hands to
// DoIO()/SendIO(); the device that OpenDevice() bound dispatches on the
// command. struct Device is a Library; struct Unit is a MsgPort + flags.
// Field order/offsets are ABI.
//
// Source: AmigaOS RKM Includes & Autodocs 3rd Edition, exec/io.h.

#ifndef EXEC_IO_H
#define EXEC_IO_H

#include <exec/libraries.h>
#include <exec/ports.h>
#include <exec/types.h>

// A device is (binary-compatibly) a Library; CaraOS hangs the
// kernel-side dispatch off a CaraDevice whose pub is this (cara/device.h).
struct Device {
    struct Library dd_Library;
};

// A device unit — a MsgPort the device's task waits on, plus open count.
// v0 devices are single-unit and synchronous, so this is mostly inert.
struct Unit {
    struct MsgPort unit_MsgPort;
    UBYTE unit_flags;
    UBYTE unit_pad;
    UWORD unit_OpenCnt;
};

// The base IO request (commands with no data payload use this).
struct IORequest {
    struct Message io_Message;
    struct Device *io_Device; // set by OpenDevice
    struct Unit *io_Unit;     // set by OpenDevice
    UWORD io_Command;         // CMD_* / device-specific
    UBYTE io_Flags;           // IOF_*
    BYTE io_Error;            // 0 = success, else IOERR_* / device-specific
};

// The standard IO request (read/write commands with a data buffer).
struct IOStdReq {
    struct Message io_Message;
    struct Device *io_Device;
    struct Unit *io_Unit;
    UWORD io_Command;
    UBYTE io_Flags;
    BYTE io_Error;
    ULONG io_Actual; // bytes actually transferred
    ULONG io_Length; // requested length
    APTR io_Data;    // the buffer
    ULONG io_Offset; // block offset (block devices)
};

// io_Flags bits.
#define IOB_QUICK 0
#define IOF_QUICK (1 << IOB_QUICK)

// Standard device commands (io_Command). Device-specific commands start
// at CMD_NONSTD.
#define CMD_INVALID 0
#define CMD_RESET 1
#define CMD_READ 2
#define CMD_WRITE 3
#define CMD_UPDATE 4
#define CMD_CLEAR 5
#define CMD_STOP 6
#define CMD_START 7
#define CMD_FLUSH 8
#define CMD_NONSTD 9

// Standard io_Error codes.
#define IOERR_OPENFAIL (-1)  // device/unit failed to open
#define IOERR_ABORTED (-2)   // request aborted
#define IOERR_NOCMD (-3)     // command not supported
#define IOERR_BADLENGTH (-4) // bad io_Length
#define IOERR_BADADDRESS (-5)

#endif // EXEC_IO_H
