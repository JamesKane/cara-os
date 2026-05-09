// SPDX-License-Identifier: BSD-2-Clause
//
// HID boot-protocol decoders. Phase 1 forces every dispatch-eligible
// HID interface into Boot mode (HA.1), which fixes the report layout
// to 8 bytes for both keyboards and mice. The decoders here translate
// those raw bytes into V36+ input-event-shaped records that the Tier 3
// HID Gleas (when it lands post-Phase 3) can `PutMsg` into Leargas's
// input port without further interpretation.
//
// Sources:
//   USB HID 1.11, §B.1 (boot keyboard) and §B.2 (boot mouse) — the
//     wire format that SET_PROTOCOL(Boot) pins to.
//   AmigaOS RKM Devices 3rd Edition, §10 (Input Device) — the V36+
//     IECLASS_RAWMOUSE / IECLASS_RAWKEY surface and the IEQUALIFIER
//     bit assignments these decoders target.

#ifndef CARA_HID_H
#define CARA_HID_H

#include <cara/types.h>

// ---- USB HID Boot-protocol report layouts ---------------------------------

constexpr u32 HID_BOOT_REPORT_BYTES = 8;

// Boot keyboard report (USB HID 1.11 §B.1):
//   byte 0       — modifier bitmap
//   byte 1       — reserved (0)
//   bytes 2..7   — up to 6 simultaneously-pressed key Usage IDs;
//                  zero-padded.
constexpr u32 HID_BOOT_KBD_MAX_KEYS = 6;

// Boot mouse report (USB HID 1.11 §B.2):
//   byte 0       — buttons[0..7]; bit 0 = left, 1 = right, 2 = middle
//   byte 1       — int8 X delta
//   byte 2       — int8 Y delta
//   byte 3       — int8 wheel delta (some mice — boot mode strictly
//                                    speaks bytes 0..2 only, but
//                                    QEMU's usb-mouse emits a 4-byte
//                                    report including wheel)

// Modifier byte bits (USB HID Usage Table §10).
enum {
    HID_MOD_LCTRL = (1u << 0),
    HID_MOD_LSHIFT = (1u << 1),
    HID_MOD_LALT = (1u << 2),
    HID_MOD_LGUI = (1u << 3),
    HID_MOD_RCTRL = (1u << 4),
    HID_MOD_RSHIFT = (1u << 5),
    HID_MOD_RALT = (1u << 6),
    HID_MOD_RGUI = (1u << 7),
};

// Mouse button bits.
enum {
    HID_MOUSE_BTN_LEFT = (1u << 0),
    HID_MOUSE_BTN_RIGHT = (1u << 1),
    HID_MOUSE_BTN_MIDDLE = (1u << 2),
};

// ---- V36+ IECLASS_RAW* surface --------------------------------------------
//
// These match the AmigaOS V36+ <devices/inputevent.h> values that the
// decoders translate into. The Tier 3 HID Gleas will `PutMsg` either a
// raw `struct InputEvent` (V36+ shape) or a thin Cara-side surface
// here; for now the decoders just produce the field values.
//
// IEQUALIFIER bits (kept as canonical Amiga values).
enum {
    IEQUALIFIER_LSHIFT = 0x0001,
    IEQUALIFIER_RSHIFT = 0x0002,
    IEQUALIFIER_CAPSLOCK = 0x0004,
    IEQUALIFIER_CONTROL = 0x0008,
    IEQUALIFIER_LALT = 0x0010,
    IEQUALIFIER_RALT = 0x0020,
    IEQUALIFIER_LCOMMAND = 0x0040, // left Amiga / Win
    IEQUALIFIER_RCOMMAND = 0x0080, // right Amiga / Win
    IEQUALIFIER_NUMERICPAD = 0x0100,
    IEQUALIFIER_REPEAT = 0x0200,
    IEQUALIFIER_INTERRUPT = 0x0400,
    IEQUALIFIER_MULTIBROADCAST = 0x0800,
    IEQUALIFIER_MIDBUTTON = 0x1000,
    IEQUALIFIER_RBUTTON = 0x2000,
    IEQUALIFIER_LEFTBUTTON = 0x4000,
    IEQUALIFIER_RELATIVEMOUSE = 0x8000,
};

// IECLASS_RAWKEY: ie_Code carries the V36+ rawkey number, with bit 7
// indicating up-stroke (key release). RAWKEY codes < 0x80 are the
// hardware-set; bit 7 is the up-stroke flag.
constexpr u8 IECODE_UP_PREFIX = 0x80;

// IECLASS_RAWMOUSE: ie_Code carries one of these subtypes for button
// transitions; mouse-move events use IECODE_NOBUTTON and rely on
// ie_X / ie_Y deltas. (Values match V36+ <devices/inputevent.h>.)
enum {
    IECODE_NOBUTTON = 0xFF,
    IECODE_LBUTTON = 0x68,
    IECODE_RBUTTON = 0x69,
    IECODE_MBUTTON = 0x6A,
};

// V36+ rawkey value returned from Croi_Hid_UsageToRawKey when a USB
// HID Usage ID has no Amiga equivalent (e.g. PrintScreen). Callers
// drop these events.
constexpr u8 CARA_RAWKEY_NONE = 0xFFu;

// ---- Decoded report structs ----------------------------------------------

struct CaraHidMouseReport {
    u8 buttons; // HID_MOUSE_BTN_* bitmap
    i8 dx;      // X delta (8-bit signed)
    i8 dy;      // Y delta (8-bit signed)
    i8 wheel;   // wheel delta (0 if not present)
    // V36+ IEQUALIFIER bits derived from `buttons` (LEFTBUTTON /
    // RBUTTON / MIDBUTTON ORed in for currently-held buttons,
    // plus IEQUALIFIER_RELATIVEMOUSE always since boot reports are
    // relative deltas).
    u16 ie_qualifier;
};

struct CaraHidKeyboardReport {
    u8 modifiers;                   // HID_MOD_* bitmap (raw byte 0)
    u8 keys[HID_BOOT_KBD_MAX_KEYS]; // up to 6 Usage IDs (raw bytes 2..7)
    // V36+ IEQUALIFIER bits derived from `modifiers`.
    u16 ie_qualifier;
};

// ---- Decoder API ----------------------------------------------------------
//
// Both decoders are pure: no I/O, no allocations, no globals.
//
//   Returns CARA_EOK on success.
//   Returns CARA_EINVAL if `raw` is null or `out` is null.

[[nodiscard]] int Croi_Hid_DecodeMouseBoot(const u8 *raw, u32 raw_len,
                                           struct CaraHidMouseReport *out);

[[nodiscard]] int Croi_Hid_DecodeKeyboardBoot(const u8 *raw, u32 raw_len,
                                              struct CaraHidKeyboardReport *out);

// USB HID Usage Page 0x07 (Keyboard/Keypad) → V36+ rawkey number.
// Returns CARA_RAWKEY_NONE for usage IDs that have no Amiga
// equivalent. Callers wrap the return value with IECODE_UP_PREFIX
// when synthesising up-stroke events.
//
// The translation table covers what every QEMU `sendkey`-injectable
// key produces; HID Usage IDs outside that range fall back to
// CARA_RAWKEY_NONE.
u8 Croi_Hid_UsageToRawKey(u8 usage_id);

#endif
