// SPDX-License-Identifier: BSD-2-Clause
//
// CaraOS device subsystem (L6.1, docs/CROI_DEVICES.md): the kernel device
// registry + the exec IO primitives (OpenDevice/DoIO/…). v0 is
// synchronous (§2.2): DoIO/SendIO run the device's BeginIO directly (the
// cooperative single-hart scheduler makes it atomic, like the L3.3 dos
// fast path); the async verbs degenerate.

#include <cara/device.h>
#include <cara/exec_lib.h> // Croi_ReplyMsg_Impl
#include <cara/types.h>
#include <exec/io.h>
#include <exec/ports.h>
#include <exec/types.h>

#define CARA_MAX_DEVICES 16
static struct CaraDevice *g_devices[CARA_MAX_DEVICES];
static u32 g_device_count;

static bool dev_name_eq(const char *a, const char *b)
{
    if (!a || !b) {
        return false;
    }
    while (*a && *a == *b) {
        a++;
        b++;
    }
    return *a == *b;
}

void Croi_Device_Register(struct CaraDevice *dev)
{
    if (!dev || g_device_count >= CARA_MAX_DEVICES) {
        return;
    }
    if (Croi_Device_Find(dev->name)) {
        return; // already registered — idempotent
    }
    g_devices[g_device_count++] = dev;
}

[[nodiscard]] struct CaraDevice *Croi_Device_Find(const char *name)
{
    for (u32 i = 0; i < g_device_count; i++) {
        if (g_devices[i] && dev_name_eq(g_devices[i]->name, name)) {
            return g_devices[i];
        }
    }
    return nullptr;
}

// Recover the CaraDevice from an opened IORequest (pub is at offset 0).
static struct CaraDevice *dev_of(struct IORequest *io)
{
    return (io && io->io_Device) ? (struct CaraDevice *)io->io_Device : nullptr;
}

// OpenDevice(name, unit, io, flags) → io_Error (0 = success). Binds the
// named device into the IORequest.
LONG Croi_OpenDevice_Impl(STRPTR name, ULONG unit, struct IORequest *io, ULONG flags)
{
    if (!io) {
        return IOERR_OPENFAIL;
    }
    struct CaraDevice *dev = Croi_Device_Find((const char *)name);
    if (!dev) {
        io->io_Error = IOERR_OPENFAIL;
        return IOERR_OPENFAIL;
    }
    io->io_Device = &dev->pub;
    io->io_Unit = nullptr; // v0: single unit per device
    BYTE err = dev->open ? dev->open(io, unit, flags) : 0;
    io->io_Error = err;
    if (err != 0) {
        io->io_Device = nullptr; // open failed → leave it unbound
    }
    return err;
}

void Croi_CloseDevice_Impl(struct IORequest *io)
{
    struct CaraDevice *dev = dev_of(io);
    if (!dev) {
        return;
    }
    if (dev->close) {
        dev->close(io);
    }
    io->io_Device = nullptr;
    io->io_Unit = nullptr;
}

// DoIO(io) — run the command synchronously, return io_Error.
LONG Croi_DoIO_Impl(struct IORequest *io)
{
    struct CaraDevice *dev = dev_of(io);
    if (!dev) {
        return IOERR_OPENFAIL;
    }
    io->io_Error = 0;
    io->io_Flags |= IOF_QUICK;
    if (dev->beginio) {
        dev->beginio(io);
    }
    return io->io_Error;
}

// SendIO(io) — v0 runs synchronously then replies to the request's reply
// port (so a WaitPort+GetMsg on it still completes).
void Croi_SendIO_Impl(struct IORequest *io)
{
    struct CaraDevice *dev = dev_of(io);
    if (!dev) {
        return;
    }
    io->io_Error = 0;
    io->io_Flags &= (UBYTE)~IOF_QUICK; // queued, not quick
    if (dev->beginio) {
        dev->beginio(io);
    }
    if (io->io_Message.mn_ReplyPort) {
        Croi_ReplyMsg_Impl(&io->io_Message);
    }
}

// CheckIO(io) — synchronous devices are always complete.
struct IORequest *Croi_CheckIO_Impl(struct IORequest *io)
{
    return io;
}

// WaitIO(io) — already complete; return io_Error.
LONG Croi_WaitIO_Impl(struct IORequest *io)
{
    return io ? io->io_Error : IOERR_OPENFAIL;
}

// AbortIO(io) — nothing is ever in flight in the synchronous v0.
void Croi_AbortIO_Impl(struct IORequest *io)
{
    (void)io;
}
