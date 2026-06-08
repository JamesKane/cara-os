// SPDX-License-Identifier: BSD-2-Clause
//
// Kernel input-pump task. The live-boot counterpart of what the smoke
// tests inject by hand: a Gleas that continuously polls the xHCI HID
// boot endpoints, decodes keyboard/mouse reports into Leargas input
// events, posts them to the L0 ring, and drains the ring so the router
// delivers IDCMP to the focused window (and moves the pointer). Phase 1
// busy-polls + yields; Phase 3 moves to interrupt-driven HID.

#ifndef CROI_INPUT_PUMP_H
#define CROI_INPUT_PUMP_H

struct XhciController;
struct LeargasPointer;

// Config handed to the task via Croi_SpawnKernelTask's arg. Lives for
// the lifetime of the task (a boot-time static).
struct InputPumpCfg {
    struct XhciController *xh;
    struct LeargasPointer *pointer;
};

// KernelTaskFn entry. `arg` is a struct InputPumpCfg *. Never returns.
void Croi_InputPump_Task(void *arg);

#endif // CROI_INPUT_PUMP_H
