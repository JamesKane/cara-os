// SPDX-License-Identifier: BSD-2-Clause
//
// CaraOS device subsystem (L6, docs/CROI_DEVICES.md). A device is a
// name-registered KOBJ_DEVICE: a CaraDevice carries the exec struct
// Device (at offset 0, so io_Device round-trips via cast) plus a
// kernel-side BeginIO dispatch + open/close hooks. The exec device
// primitives (OpenDevice/DoIO/…) are syscall-flavour and live in
// src/croi/exec_lib/device.c; they look devices up in the kernel
// registry and call BeginIO directly (v0: synchronous, §2.2).

#ifndef CARA_DEVICE_H
#define CARA_DEVICE_H

#include <cara/types.h>
#include <exec/io.h>
#include <exec/types.h>

// A kernel device. pub is the exec struct Device at offset 0 — the
// IORequest's io_Device points here and casts straight back to a
// CaraDevice * (the dos/intuition ext-head pattern).
struct CaraDevice {
    struct Device pub;
    const char *name;                                            // "timer.device", …
    void (*beginio)(struct IORequest *io);                       // command dispatch (kernel)
    BYTE (*open)(struct IORequest *io, ULONG unit, ULONG flags); // 0 = ok
    void (*close)(struct IORequest *io);
};

// Add a device to the kernel registry (call at boot). Idempotent-ish:
// duplicates by name are allowed but Find returns the first.
void Croi_Device_Register(struct CaraDevice *dev);

// Look a device up by name; nullptr if not registered.
[[nodiscard]] struct CaraDevice *Croi_Device_Find(const char *name);

// ---- exec device primitives (L6.1, syscall flavour) ------------------
LONG Croi_OpenDevice_Impl(STRPTR name, ULONG unit, struct IORequest *io, ULONG flags);
void Croi_CloseDevice_Impl(struct IORequest *io);
LONG Croi_DoIO_Impl(struct IORequest *io);
void Croi_SendIO_Impl(struct IORequest *io);
struct IORequest *Croi_CheckIO_Impl(struct IORequest *io);
LONG Croi_WaitIO_Impl(struct IORequest *io);
void Croi_AbortIO_Impl(struct IORequest *io);

// ---- Device drivers — boot registration (L6.2+) ----------------------
// Each registers its CaraDevice with the registry; call at boot (entry.c)
// before the test runner / any OpenDevice. Idempotent (Register skips a
// name already present).
void Croi_Timer_Init(void);   // timer.device (L6.2)
void Croi_Console_Init(void); // console.device (L6.3)
void Croi_Input_Init(void);   // input.device (L6.4)

#endif // CARA_DEVICE_H
