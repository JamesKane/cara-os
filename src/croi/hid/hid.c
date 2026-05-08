// SPDX-License-Identifier: BSD-2-Clause
//
// HID boot-protocol decoders (HA.2 / HA.3) and the USB HID Usage Page
// 0x07 (Keyboard/Keypad) → V36+ rawkey translation table (HA.4). Pure
// logic, no I/O — the kernel-side bring-up substrate uses these
// directly; once the Tier 3 HID Gleas moves to user space (post HB.1)
// the same functions link against the V36+ trampolines that surface
// `usb.device` to U-mode.
//
// Cleanroom from:
//   USB HID 1.11 §B.1 / §B.2 (boot keyboard / mouse layouts)
//   USB HID Usage Tables 1.5 §10 (keyboard usage IDs)
//   AmigaOS RKM Devices 3rd Edition §10 (input.device — the V36+
//     IECLASS_RAWMOUSE / IECLASS_RAWKEY surface and IEQUALIFIER bits)
//
// The Amiga rawkey assignments here match the canonical
// <devices/keymap.h> values shipped with the Amiga 3rd edition RKMs
// (each printable key has a hardware-fixed numeric code; the keymap
// library translates rawkey → ANSI on top).

#include <cara/hid.h>
#include <cara/types.h>

// ---- USB Usage ID (page 0x07) → V36+ rawkey number ------------------------
//
// 256-entry table indexed by the byte from the boot keyboard report.
// Anything not assigned reads as CARA_RAWKEY_NONE (0xFF) and the
// caller drops the event.

static const u8 g_usage_to_rawkey[256] = {
    // 0x00..0x03 — reserved / ErrorRollOver / POSTFail / ErrorUndefined
    [0x00] = CARA_RAWKEY_NONE, [0x01] = CARA_RAWKEY_NONE,
    [0x02] = CARA_RAWKEY_NONE, [0x03] = CARA_RAWKEY_NONE,

    // 0x04..0x1D — letters a..z
    [0x04] = 0x20,  [0x05] = 0x35,  [0x06] = 0x33,  [0x07] = 0x22,
    [0x08] = 0x12,  [0x09] = 0x23,  [0x0A] = 0x24,  [0x0B] = 0x25,
    [0x0C] = 0x17,  [0x0D] = 0x26,  [0x0E] = 0x27,  [0x0F] = 0x28,
    [0x10] = 0x37,  [0x11] = 0x36,  [0x12] = 0x18,  [0x13] = 0x19,
    [0x14] = 0x10,  [0x15] = 0x13,  [0x16] = 0x21,  [0x17] = 0x14,
    [0x18] = 0x16,  [0x19] = 0x34,  [0x1A] = 0x11,  [0x1B] = 0x32,
    [0x1C] = 0x15,  [0x1D] = 0x31,

    // 0x1E..0x27 — digits 1..0 (top row)
    [0x1E] = 0x01,  [0x1F] = 0x02,  [0x20] = 0x03,  [0x21] = 0x04,
    [0x22] = 0x05,  [0x23] = 0x06,  [0x24] = 0x07,  [0x25] = 0x08,
    [0x26] = 0x09,  [0x27] = 0x0A,

    // 0x28..0x2C — Enter / Escape / Backspace / Tab / Space
    [0x28] = 0x44,  [0x29] = 0x45,  [0x2A] = 0x41,
    [0x2B] = 0x42,  [0x2C] = 0x40,

    // 0x2D..0x38 — symbols
    [0x2D] = 0x0B,  // -_
    [0x2E] = 0x0C,  // =+
    [0x2F] = 0x1A,  // [{
    [0x30] = 0x1B,  // ]}
    [0x31] = 0x0D,  // \|
    [0x32] = 0x2B,  // # (non-US hash)
    [0x33] = 0x29,  // ;:
    [0x34] = 0x2A,  // '"
    [0x35] = 0x00,  // `~
    [0x36] = 0x38,  // ,<
    [0x37] = 0x39,  // .>
    [0x38] = 0x3A,  // /?

    // 0x39 — CapsLock
    [0x39] = 0x62,

    // 0x3A..0x43 — F1..F10
    [0x3A] = 0x50,  [0x3B] = 0x51,  [0x3C] = 0x52,  [0x3D] = 0x53,
    [0x3E] = 0x54,  [0x3F] = 0x55,  [0x40] = 0x56,  [0x41] = 0x57,
    [0x42] = 0x58,  [0x43] = 0x59,
    // F11/F12 have no Amiga equivalent.
    [0x44] = CARA_RAWKEY_NONE, [0x45] = CARA_RAWKEY_NONE,

    // 0x4F..0x52 — arrows
    [0x4F] = 0x4E,  // Right
    [0x50] = 0x4F,  // Left
    [0x51] = 0x4D,  // Down
    [0x52] = 0x4C,  // Up

    // 0x4A — Home (no canonical Amiga equivalent on stock A500;
    //         leave NONE).
    [0x4A] = CARA_RAWKEY_NONE,

    // 0xE0..0xE7 — modifier keys (also reflected in report byte 0)
    [0xE0] = 0x63,  // LCtrl  → CTRL (Amiga has a single Ctrl rawkey)
    [0xE1] = 0x60,  // LShift
    [0xE2] = 0x64,  // LAlt
    [0xE3] = 0x66,  // LGUI / Left Amiga
    [0xE4] = 0x63,  // RCtrl  → CTRL (same code as LCtrl on Amiga)
    [0xE5] = 0x61,  // RShift
    [0xE6] = 0x65,  // RAlt
    [0xE7] = 0x67,  // RGUI / Right Amiga
};

u8 Croi_Hid_UsageToRawKey(u8 usage_id)
{
    return g_usage_to_rawkey[usage_id];
}

// ---- Modifier byte → IEQUALIFIER bits ------------------------------------

static u16 modifier_to_iequalifier(u8 mod)
{
    u16 q = 0;
    if (mod & HID_MOD_LSHIFT) q |= IEQUALIFIER_LSHIFT;
    if (mod & HID_MOD_RSHIFT) q |= IEQUALIFIER_RSHIFT;
    // Amiga collapses LCTRL/RCTRL into one CONTROL bit.
    if (mod & (HID_MOD_LCTRL | HID_MOD_RCTRL)) q |= IEQUALIFIER_CONTROL;
    if (mod & HID_MOD_LALT)   q |= IEQUALIFIER_LALT;
    if (mod & HID_MOD_RALT)   q |= IEQUALIFIER_RALT;
    if (mod & HID_MOD_LGUI)   q |= IEQUALIFIER_LCOMMAND;
    if (mod & HID_MOD_RGUI)   q |= IEQUALIFIER_RCOMMAND;
    return q;
}

[[nodiscard]] int Croi_Hid_DecodeKeyboardBoot(const u8 *raw, u32 raw_len,
                                              struct CaraHidKeyboardReport *out)
{
    if (!raw || !out) {
        return CARA_EINVAL;
    }
    if (raw_len < HID_BOOT_REPORT_BYTES) {
        return CARA_EINVAL;
    }

    out->modifiers    = raw[0];
    out->ie_qualifier = modifier_to_iequalifier(raw[0]);
    // raw[1] is reserved (always 0 in boot mode).
    for (u32 i = 0; i < HID_BOOT_KBD_MAX_KEYS; i++) {
        out->keys[i] = raw[2 + i];
    }
    return CARA_EOK;
}

// ---- Mouse boot-protocol decoder -----------------------------------------

[[nodiscard]] int Croi_Hid_DecodeMouseBoot(const u8 *raw, u32 raw_len,
                                           struct CaraHidMouseReport *out)
{
    if (!raw || !out) {
        return CARA_EINVAL;
    }
    // USB HID 1.11 §B.2 mandates 3 bytes minimum (buttons + dx + dy).
    // QEMU's usb-mouse adds a 4th wheel byte; the boot spec doesn't
    // guarantee it, so we treat it as optional.
    if (raw_len < 3) {
        return CARA_EINVAL;
    }
    out->buttons = raw[0];
    out->dx      = (i8)raw[1];
    out->dy      = (i8)raw[2];
    out->wheel   = (raw_len >= 4) ? (i8)raw[3] : (i8)0;

    u16 q = IEQUALIFIER_RELATIVEMOUSE;
    if (out->buttons & HID_MOUSE_BTN_LEFT)   q |= IEQUALIFIER_LEFTBUTTON;
    if (out->buttons & HID_MOUSE_BTN_RIGHT)  q |= IEQUALIFIER_RBUTTON;
    if (out->buttons & HID_MOUSE_BTN_MIDDLE) q |= IEQUALIFIER_MIDBUTTON;
    out->ie_qualifier = q;
    return CARA_EOK;
}
