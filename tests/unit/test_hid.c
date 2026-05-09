// SPDX-License-Identifier: BSD-2-Clause
//
// Hosted unit test for cara/hid.h. Exercises the boot-protocol
// decoders (mouse + keyboard) against synthetic byte arrays and
// spot-checks the USB Usage → V36+ rawkey table on every key QEMU's
// `sendkey` driver emits for the test harness.

#include <cara/hid.h>
#include <stdio.h>
#include <string.h>

static int fail(const char *msg)
{
    fprintf(stderr, "test_hid: FAIL: %s\n", msg);
    return 1;
}

static int test_mouse_boot_idle(void)
{
    // All-zero report = no buttons, no movement, no wheel.
    const u8 raw[4] = { 0, 0, 0, 0 };
    struct CaraHidMouseReport r;
    if (Croi_Hid_DecodeMouseBoot(raw, sizeof(raw), &r) != CARA_EOK) {
        return fail("mouse_boot_idle: decode returned non-EOK");
    }
    if (r.buttons != 0 || r.dx != 0 || r.dy != 0 || r.wheel != 0) {
        return fail("mouse_boot_idle: non-zero state from zero report");
    }
    if (r.ie_qualifier != IEQUALIFIER_RELATIVEMOUSE) {
        return fail("mouse_boot_idle: ie_qualifier missing RELATIVEMOUSE");
    }
    return 0;
}

static int test_mouse_boot_left_click_with_motion(void)
{
    // Button 0 (left) held, dx=+5, dy=-3, wheel=+1.
    const u8 raw[4] = { HID_MOUSE_BTN_LEFT, 5, (u8)(i8)-3, 1 };
    struct CaraHidMouseReport r;
    if (Croi_Hid_DecodeMouseBoot(raw, sizeof(raw), &r) != CARA_EOK) {
        return fail("mouse left-click: decode failed");
    }
    if (r.buttons != HID_MOUSE_BTN_LEFT)
        return fail("buttons mismatch");
    if (r.dx != 5)
        return fail("dx mismatch");
    if (r.dy != -3)
        return fail("dy mismatch (sign extension?)");
    if (r.wheel != 1)
        return fail("wheel mismatch");
    if (!(r.ie_qualifier & IEQUALIFIER_LEFTBUTTON) ||
        !(r.ie_qualifier & IEQUALIFIER_RELATIVEMOUSE)) {
        return fail("ie_qualifier missing LEFTBUTTON|RELATIVEMOUSE");
    }
    if (r.ie_qualifier & IEQUALIFIER_RBUTTON) {
        return fail("RBUTTON spuriously set");
    }
    return 0;
}

static int test_mouse_boot_short_report_3byte(void)
{
    // USB HID 1.11 §B.2 minimum: 3 bytes, no wheel. The decoder
    // must accept it and zero out the wheel field.
    const u8 raw[3] = { HID_MOUSE_BTN_RIGHT | HID_MOUSE_BTN_MIDDLE, (u8)(i8)127, (u8)(i8)-128 };
    struct CaraHidMouseReport r;
    if (Croi_Hid_DecodeMouseBoot(raw, sizeof(raw), &r) != CARA_EOK) {
        return fail("3-byte mouse: decode failed");
    }
    if (r.dx != 127 || r.dy != -128) {
        return fail("3-byte mouse: dx/dy bounds wrong");
    }
    if (r.wheel != 0) {
        return fail("3-byte mouse: wheel not zeroed");
    }
    if (!(r.ie_qualifier & IEQUALIFIER_RBUTTON) || !(r.ie_qualifier & IEQUALIFIER_MIDBUTTON)) {
        return fail("3-byte mouse: button qualifiers missing");
    }
    if (r.ie_qualifier & IEQUALIFIER_LEFTBUTTON) {
        return fail("3-byte mouse: LEFTBUTTON spuriously set");
    }
    return 0;
}

static int test_mouse_boot_too_short(void)
{
    const u8 raw[2] = { 0, 0 };
    struct CaraHidMouseReport r;
    if (Croi_Hid_DecodeMouseBoot(raw, sizeof(raw), &r) != CARA_EINVAL) {
        return fail("2-byte mouse should be EINVAL");
    }
    return 0;
}

static int test_keyboard_boot_idle(void)
{
    const u8 raw[8] = { 0, 0, 0, 0, 0, 0, 0, 0 };
    struct CaraHidKeyboardReport r;
    if (Croi_Hid_DecodeKeyboardBoot(raw, sizeof(raw), &r) != CARA_EOK) {
        return fail("kbd idle: decode failed");
    }
    if (r.modifiers != 0 || r.ie_qualifier != 0) {
        return fail("kbd idle: non-zero state from idle report");
    }
    for (u32 i = 0; i < HID_BOOT_KBD_MAX_KEYS; i++) {
        if (r.keys[i] != 0)
            return fail("kbd idle: nonzero key slot");
    }
    return 0;
}

static int test_keyboard_boot_lshift_a(void)
{
    // Left Shift held + 'a' (Usage 0x04) pressed.
    const u8 raw[8] = { HID_MOD_LSHIFT, 0, 0x04, 0, 0, 0, 0, 0 };
    struct CaraHidKeyboardReport r;
    if (Croi_Hid_DecodeKeyboardBoot(raw, sizeof(raw), &r) != CARA_EOK) {
        return fail("kbd shift+a: decode failed");
    }
    if (r.modifiers != HID_MOD_LSHIFT)
        return fail("modifiers mismatch");
    if (!(r.ie_qualifier & IEQUALIFIER_LSHIFT)) {
        return fail("ie_qualifier missing LSHIFT");
    }
    if (r.ie_qualifier & IEQUALIFIER_RSHIFT) {
        return fail("ie_qualifier RSHIFT spuriously set");
    }
    if (r.keys[0] != 0x04)
        return fail("first key slot != 'a' usage");
    return 0;
}

static int test_keyboard_boot_ctrl_collapses(void)
{
    // Both Ctrls held — Amiga collapses to single CONTROL bit.
    const u8 raw[8] = { HID_MOD_LCTRL | HID_MOD_RCTRL, 0, 0, 0, 0, 0, 0, 0 };
    struct CaraHidKeyboardReport r;
    (void)Croi_Hid_DecodeKeyboardBoot(raw, sizeof(raw), &r);
    if (!(r.ie_qualifier & IEQUALIFIER_CONTROL)) {
        return fail("kbd ctrl: missing CONTROL");
    }
    return 0;
}

static int test_keyboard_boot_six_key_rollover(void)
{
    // Six keys held simultaneously: a..f (Usages 0x04..0x09).
    const u8 raw[8] = { 0, 0, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09 };
    struct CaraHidKeyboardReport r;
    if (Croi_Hid_DecodeKeyboardBoot(raw, sizeof(raw), &r) != CARA_EOK) {
        return fail("kbd 6-rollover: decode failed");
    }
    for (u32 i = 0; i < HID_BOOT_KBD_MAX_KEYS; i++) {
        if (r.keys[i] != (u8)(0x04 + i)) {
            return fail("kbd 6-rollover: key slot wrong");
        }
    }
    return 0;
}

static int test_keyboard_boot_too_short(void)
{
    const u8 raw[7] = { 0 };
    struct CaraHidKeyboardReport r;
    if (Croi_Hid_DecodeKeyboardBoot(raw, sizeof(raw), &r) != CARA_EINVAL) {
        return fail("7-byte kbd should be EINVAL");
    }
    return 0;
}

static int test_usage_to_rawkey_letters(void)
{
    // Spot-check: a (0x04) → 0x20, z (0x1D) → 0x31.
    if (Croi_Hid_UsageToRawKey(0x04) != 0x20)
        return fail("'a' rawkey wrong");
    if (Croi_Hid_UsageToRawKey(0x1D) != 0x31)
        return fail("'z' rawkey wrong");
    if (Croi_Hid_UsageToRawKey(0x16) != 0x21)
        return fail("'s' rawkey wrong");
    if (Croi_Hid_UsageToRawKey(0x07) != 0x22)
        return fail("'d' rawkey wrong");
    return 0;
}

static int test_usage_to_rawkey_digits(void)
{
    // 1..9 → 0x01..0x09; 0 → 0x0A.
    for (u8 d = 1; d <= 9; d++) {
        u8 usage = (u8)(0x1E + d - 1);
        if (Croi_Hid_UsageToRawKey(usage) != d) {
            return fail("digit rawkey wrong");
        }
    }
    if (Croi_Hid_UsageToRawKey(0x27) != 0x0A) {
        return fail("'0' rawkey wrong");
    }
    return 0;
}

static int test_usage_to_rawkey_specials(void)
{
    if (Croi_Hid_UsageToRawKey(0x28) != 0x44)
        return fail("Enter wrong");
    if (Croi_Hid_UsageToRawKey(0x29) != 0x45)
        return fail("Escape wrong");
    if (Croi_Hid_UsageToRawKey(0x2A) != 0x41)
        return fail("Backspace wrong");
    if (Croi_Hid_UsageToRawKey(0x2B) != 0x42)
        return fail("Tab wrong");
    if (Croi_Hid_UsageToRawKey(0x2C) != 0x40)
        return fail("Space wrong");
    if (Croi_Hid_UsageToRawKey(0x4F) != 0x4E)
        return fail("Right wrong");
    if (Croi_Hid_UsageToRawKey(0x50) != 0x4F)
        return fail("Left wrong");
    if (Croi_Hid_UsageToRawKey(0x51) != 0x4D)
        return fail("Down wrong");
    if (Croi_Hid_UsageToRawKey(0x52) != 0x4C)
        return fail("Up wrong");
    return 0;
}

static int test_usage_to_rawkey_unmapped(void)
{
    // 0x00 (reserved) and F11 (0x44) — no Amiga equivalent.
    if (Croi_Hid_UsageToRawKey(0x00) != CARA_RAWKEY_NONE) {
        return fail("0x00 should be NONE");
    }
    if (Croi_Hid_UsageToRawKey(0x44) != CARA_RAWKEY_NONE) {
        return fail("F11 should be NONE");
    }
    return 0;
}

int main(void)
{
    int rc = 0;
    rc |= test_mouse_boot_idle();
    rc |= test_mouse_boot_left_click_with_motion();
    rc |= test_mouse_boot_short_report_3byte();
    rc |= test_mouse_boot_too_short();
    rc |= test_keyboard_boot_idle();
    rc |= test_keyboard_boot_lshift_a();
    rc |= test_keyboard_boot_ctrl_collapses();
    rc |= test_keyboard_boot_six_key_rollover();
    rc |= test_keyboard_boot_too_short();
    rc |= test_usage_to_rawkey_letters();
    rc |= test_usage_to_rawkey_digits();
    rc |= test_usage_to_rawkey_specials();
    rc |= test_usage_to_rawkey_unmapped();
    if (rc == 0) {
        printf("test_hid: OK\n");
    }
    return rc;
}
