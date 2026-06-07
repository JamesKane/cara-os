// SPDX-License-Identifier: BSD-2-Clause
//
// Leargas — the brand-namespace Phase 1 minimum window-system
// substrate. Phase 3's `intuition.library` LVO surface trampolines
// into the same `Leargas_*` symbols this header declares (see
// docs/PHASE1_LEARGAS.md and ARCHITECTURE.md §11).
//
// L0 — Input event ring. The seam between the (Phase 1: kernel HID
// poll, Phase 3: HID Gleas) producer side and the (Phase 1: pointer
// + window router, Phase 3: input.device → intuition) consumer side.
// SPSC by contract: exactly one producer task and one consumer task
// at a time. Phase 1 has both running in-kernel; Phase 3 moves the
// producer to a U-mode HID Gleas without changing the contract.

#ifndef CARA_LEARGAS_H
#define CARA_LEARGAS_H

#include <cara/dath.h>
#include <cara/types.h>
#include <intuition/intuition.h>
#include <intuition/screens.h>

// ---- Flat input event record ----------------------------------------------
//
// Brand-namespace flat shape that flows through the Phase 1 input
// ring. Distinct from V36+ `struct InputEvent` (which is a linked-list
// node with an embedded `struct timeval`); when Phase 3 publishes
// input.device the LVO body translates between the two.
//
// Field semantics, by ie_class:
//
//   IECLASS_RAWKEY:   ie_code = V36+ rawkey number (bit 7 = up-stroke
//                     via IECODE_UP_PREFIX). ie_qualifier = IEQUALIFIER_*
//                     modifier bitmap. ie_dx/dy unused.
//   IECLASS_RAWMOUSE: ie_code = IECODE_LBUTTON / RBUTTON / MBUTTON
//                     for transitions, IECODE_NOBUTTON for motion-only.
//                     ie_qualifier = IEQUALIFIER_RELATIVEMOUSE plus
//                     held-button bits. ie_dx/dy = signed pixel deltas.
//
// Sized so that producers can construct one on the stack with a
// trivial sequence of stores; the impl copies it byte-for-byte into
// the ring slot.

struct LeargasInputEvent {
    u8 ie_class;      // IECLASS_*
    u8 ie_subclass;   // IESUBCLASS_* (default IESUBCLASS_COMPATIBLE = 0)
    u16 ie_code;      // IECODE_* / rawkey
    u16 ie_qualifier; // IEQUALIFIER_*
    u16 _ie_pad0;     // explicit pad — keeps the layout deterministic
    i16 ie_dx;        // RAWMOUSE: X delta (pixels)
    i16 ie_dy;        // RAWMOUSE: Y delta (pixels)
    u32 _ie_pad1;     // explicit pad — see above
    u64 ie_ts_ns;     // boot-time nanoseconds; producer fills from Croi_Time_Now
};
static_assert(sizeof(struct LeargasInputEvent) == 24,
              "LeargasInputEvent layout drifted; ring slot pun depends on it");

// ---- Phase 1 input-ring API ------------------------------------------------
//
// The ring is a process-wide singleton in Phase 1: there is exactly
// one input event stream and one consumer (the Leargas window-event
// router). Initialised once at boot; subsequent Init calls are
// idempotent and return CARA_EOK.

constexpr u32 LEARGAS_INPUT_RING_CAP = 64;

// Initialise the input ring. Idempotent. Capacity is fixed at
// LEARGAS_INPUT_RING_CAP (64) — boot-protocol HID at typical poll
// rates (kbd ≤ 8 keys, mouse ≤ 200 Hz) leaves >>40ms of headroom
// before backpressure, which is far longer than Leargas's drain
// cadence in any sensible Phase 1 configuration.
[[nodiscard]] int Leargas_Input_Init(void);

// Producer side. Returns true on success, false when the ring is
// full (caller drops the event). Wait-free; safe from a HID interrupt
// path. ev is read-only and copied; caller may free immediately.
[[nodiscard]] bool Leargas_Input_Post(const struct LeargasInputEvent *ev);

// Consumer side. Returns true on success, false when the ring is
// empty. Wait-free. Phase 1 callers poll; signal-based wake of a
// blocked consumer task arrives with LA when the pointer Gleas
// blocks on the ring. *out is written exactly when the call returns
// true.
[[nodiscard]] bool Leargas_Input_Read(struct LeargasInputEvent *out);

// Number of events queued. Lock-free snapshot — value is correct as
// of the moment of the call but may change before the caller reads
// it; Phase 1 uses this only for instrumentation and tests.
[[nodiscard]] u32 Leargas_Input_Pending(void);

// Reset the ring to empty. Test-only; production callers (HID
// producer, Leargas consumer) never invoke this. Safe to call
// before Init has run.
void Leargas_Input_Reset(void);

// ---- LA — Pointer rendering ----------------------------------------------
//
// A LeargasPointerImage is a small static glyph (typically 16×16) with
// per-pixel ternary state: transparent / foreground / background. The
// background pixels are usually the dark outline that makes a white
// arrow visible against any backdrop; transparent pixels skip the
// composite so the screen below shows through.
//
// Pixel encoding (one byte per pixel, row-major, top-left origin):
//   0 — transparent (composite skips this pixel)
//   1 — foreground (drawn in the pointer's `fg` colour)
//   2 — background (drawn in the pointer's `bg` colour)

enum : u8 {
    LEARGAS_PTR_TRANSPARENT = 0,
    LEARGAS_PTR_FG = 1,
    LEARGAS_PTR_BG = 2,
};

struct LeargasPointerImage {
    u32 width;
    u32 height;
    i32 hot_x;        // hot-spot offset within the image (left = 0)
    i32 hot_y;        // hot-spot offset within the image (top  = 0)
    const u8 *pixels; // width*height bytes; values are LEARGAS_PTR_*
};

// Default 16×16 arrow pointer with hot-spot at (0, 0). Phase 1 ships
// exactly one cursor; theming and per-window cursors land with
// Phase 3+.
extern const struct LeargasPointerImage leargas_pointer_arrow;

// Pointer state — owns no memory. The caller provides:
//   - `fb`: the screen framebuffer to composite onto.
//   - `save`: an off-screen DathFramebuffer at least as big as the
//     image (same format as `fb`); holds the pixels currently
//     under the pointer so Move can restore them. Kernel callers
//     allocate via Dath_AllocBitmap; tests can stack-mount a
//     synthetic backing buffer.
struct LeargasPointer {
    struct DathFramebuffer *fb;
    struct DathFramebuffer *save;
    const struct LeargasPointerImage *img;
    DathColor fg;
    DathColor bg;
    i32 x; // hot-spot position (visible cursor tip)
    i32 y;
    bool save_valid;
};

// Initialise the pointer state, capture the screen pixels under the
// initial position, and composite the image. Returns CARA_EINVAL on
// null pointers, mismatched formats, or save buffer smaller than
// image dimensions.
[[nodiscard]] int Leargas_Pointer_Init(struct LeargasPointer *p, struct DathFramebuffer *fb,
                                       struct DathFramebuffer *save,
                                       const struct LeargasPointerImage *img, DathColor fg,
                                       DathColor bg, i32 x0, i32 y0);

// Move the pointer hot-spot to (x, y). Restores the previously-saved
// underneath pixels at the old position, captures the underneath
// pixels at the new position, and composites the image. Off-screen
// (negative or past-edge) moves are clipped via Dath's clipping;
// the pointer state still tracks the requested coordinates so the
// next Move from off-screen back into bounds reappears correctly.
void Leargas_Pointer_Move(struct LeargasPointer *p, i32 x, i32 y);

// Lift the pointer off the framebuffer: restore the saved underneath
// pixels at the current position so chrome beneath the cursor can be
// repainted without the stale save clobbering it on the next Move.
// No-op if the pointer is already hidden (save not valid). Pairs with
// Leargas_Pointer_Show; used by the LE focus path (and LF/LG onward)
// whenever a redraw may touch pixels under the pointer.
void Leargas_Pointer_Hide(struct LeargasPointer *p);

// Re-capture the underneath pixels at the current position and
// composite the pointer image back on top. No-op if already shown.
// Pairs with Leargas_Pointer_Hide.
void Leargas_Pointer_Show(struct LeargasPointer *p);

// ---- LB — Screen substrate ------------------------------------------------
//
// LeargasScreen is the brand-namespace kernel-internal extension of
// V36+ struct Screen. The public Screen lives at offset 0 (`pub`
// field is first) so a `LeargasScreen *` is interchangeable with a
// `struct Screen *` via plain cast — same pattern as CroiMsgPort
// (src/croi/ipc/msgport_impl.h). Going the other way uses
// container_of against the pub member; future Phase 3 LVO bodies
// (Leargas_OpenScreen LVO) do exactly that.
//
// Phase 1 has exactly one screen at a time. The active-screen
// pointer is a process-wide singleton; Phase 3 evolves to a list
// of screens with depth-arrange semantics.

#define LEARGAS_SCREEN_TITLE_MAX 64

struct LeargasScreen {
    struct Screen pub; // V36+ public Screen — must be first field

    // Brand-private state.
    struct DathFramebuffer *fb; // backing surface for the screen
    char title_buf[LEARGAS_SCREEN_TITLE_MAX];
    char default_title_buf[LEARGAS_SCREEN_TITLE_MAX];
    DathColor pen0; // background colour painted by OpenScreen
    bool initialised;
};

// Initialise a caller-provided LeargasScreen from a DathFramebuffer
// and a screen title. Sets the public Screen field set with V36+
// defaults (BarHeight, WBor*, MenuVBorder/HBorder), copies the title
// into the brand-owned buffer, and stores `fb` for later rendering.
// Does NOT paint, allocate memory, or set the active screen — those
// are kernel-side concerns owned by Leargas_OpenScreen.
//
// Returns CARA_EINVAL on null arguments or a framebuffer with zero
// width / height; CARA_EOK on success.
[[nodiscard]] int Leargas_Screen_InitInPlace(struct LeargasScreen *s, struct DathFramebuffer *fb,
                                             const char *title, DathColor pen0);

// Set / clear the process-wide active screen pointer. Phase 1 has
// exactly one active screen at a time; passing nullptr clears it.
// `Leargas_ActiveScreen` returns the current public pointer (or
// nullptr if no screen is active).
void Leargas_Screen_SetActive(struct LeargasScreen *s);
[[nodiscard]] struct Screen *Leargas_ActiveScreen(void);

// Recover a LeargasScreen pointer from its public Screen pointer.
// Inline so kernel-side hot paths inline the offsetof; callers that
// hold a LeargasScreen * directly skip this helper.
[[nodiscard]] struct LeargasScreen *Leargas_Screen_FromPub(struct Screen *pub);

// Kernel-only: heap-allocate a LeargasScreen, init from `fb` + title,
// paint the screen background with `pen0`, and set as the active
// screen. Returns the public Screen pointer, or nullptr on
// allocation failure / invalid arguments.
[[nodiscard]] struct Screen *Leargas_OpenScreen(struct DathFramebuffer *fb, const char *title,
                                                DathColor pen0);

// Kernel-only: tear down a screen previously returned from
// Leargas_OpenScreen. Clears the active-screen pointer if `s` was
// active; releases the heap allocation. Safe to call on nullptr.
void Leargas_CloseScreen(struct Screen *s);

// ---- LC — Mouse motion → pointer router -----------------------------------
//
// Drain the input ring; for each IECLASS_RAWMOUSE event, accumulate
// `ie_dx / ie_dy` onto the pointer's position, clamp to the active
// screen extent (no clamp if no active screen), and call
// Leargas_Pointer_Move. Returns the number of events consumed
// (including IECLASS_RAWKEY events, which are silently dropped here
// and re-consumed by LF — but only one consumer of the ring exists
// at a time in Phase 1, so RAWKEY events that arrive before LF lands
// are lost on the floor).
//
// Phase 1 polls; Phase 3 evolves to a U-mode Leargas Gleas blocked
// on the input ring's signal kobj so motion arrives at IRQ rate
// rather than poll-loop cadence.
u32 Leargas_Input_Drain(struct LeargasPointer *p);

// ---- LD — Window primitives ----------------------------------------------
//
// LeargasWindow extends V36+ struct Window with brand-private state
// (the title buffer the public Title pointer points at, plus the
// owning screen's framebuffer cached for renderer reach). Same
// CroiMsgPort/LeargasScreen pattern: pub field at offset 0 so a
// `struct LeargasWindow *` round-trips with `struct Window *` via
// plain cast / FromPub.

#define LEARGAS_WINDOW_TITLE_MAX 64

struct LeargasWindow {
    struct Window pub; // V36+ public Window — must be first field

    // Brand-private state.
    char title_buf[LEARGAS_WINDOW_TITLE_MAX];
    i32 idcmp_sigbit; // signal bit backing pub.UserPort, or -1 if none (LF)
    bool initialised;
};

// Initialise a caller-provided LeargasWindow from a NewWindow
// descriptor. Copies the title into the brand-owned buffer; pubs
// every public field (LeftEdge/TopEdge/Width/Height, Flags,
// IDCMPFlags, MinWidth/MinHeight/MaxWidth/MaxHeight, DetailPen,
// BlockPen, WScreen, BorderLeft/Top/Right/Bottom default insets);
// leaves the V36+-deviating fields (RPort, BorderRPort, WLayer,
// MenuStrip, FirstRequest, DMRequest, IFont, Pointer) at nullptr —
// the renderer goes through the brand wrapper instead. Does not
// link into the screen's window list, allocate memory, or paint;
// those are kernel-side.
//
// Returns CARA_EINVAL on null arguments, an unset NewWindow.Screen
// AND no active screen, or zero-sized geometry; CARA_EOK on success.
[[nodiscard]] int Leargas_Window_InitInPlace(struct LeargasWindow *w, const struct NewWindow *nw);

// Render the window decorations (border, title bar, title text,
// close gadget if WFLG_CLOSEGADGET) into the owning screen's
// framebuffer. Pulls the framebuffer from
// Leargas_Screen_FromPub(w->pub.WScreen)->fb. Subsequent screen-
// level repaints (Phase 3+ Layer support) call this again.
void Leargas_Window_Render(struct LeargasWindow *w);

// Recover a LeargasWindow pointer from its public Window pointer.
[[nodiscard]] struct LeargasWindow *Leargas_Window_FromPub(struct Window *pub);

// Link `w` into the head of its screen's FirstWindow list; safe to
// call exactly once per Leargas_Window_InitInPlace.
void Leargas_Window_LinkToScreen(struct LeargasWindow *w);

// Unlink `w` from its screen's FirstWindow list; safe to call on
// a window that was never linked or has already been unlinked.
void Leargas_Window_UnlinkFromScreen(struct LeargasWindow *w);

// Kernel-only: heap-allocate a LeargasWindow, init from `nw`, link
// into the screen's window list, render decorations. Returns the
// public Window pointer or nullptr on allocation / argument
// failure. NewWindow.Screen may be nullptr — falls back to the
// active screen (Leargas_ActiveScreen).
[[nodiscard]] struct Window *Leargas_OpenWindow(const struct NewWindow *nw);

// Kernel-only: tear down a window previously returned from
// Leargas_OpenWindow. Unlinks from the screen's window list,
// blanks the screen region the window occupied (Phase 1 has no
// Layer support — Phase 3+ redraws the underneath properly),
// frees the heap allocation. Safe on nullptr.
void Leargas_CloseWindow(struct Window *w);

// ---- LE — Focus + activation ----------------------------------------------
//
// Phase 1 tracks exactly one focused (active) window, process-wide.
// A mouse button-down hit-tests the active screen's window list
// front-to-back (FirstWindow is the front-most); the topmost window
// containing the pointer becomes active and the previously-active
// window deactivates. Each window's WFLG_WINDOWACTIVE flag mirrors
// the focus state, and the title bar redraws in active vs. inactive
// chrome (Leargas_Window_Render reads the flag).
//
// Phase 1 deliberately does NOT raise the clicked window to the front
// of the list — focus changes without a depth-arrange. Overlap is
// handled by simple repaint; Layer-based z-order is Phase 3 + 4.
//
// IDCMP delivery — turning a focus change into IDCMP_ACTIVEWINDOW /
// IDCMP_INACTIVEWINDOW IntuiMessages on each window's UserPort — is
// LF's job (LF owns the per-window MsgPort + IntuiMessage shape; LD
// leaves UserPort nullptr until then). Leargas_SetActiveWindow is the
// single focus-change chokepoint LF extends to post those messages.

// Return the currently-focused window, or nullptr if none.
[[nodiscard]] struct Window *Leargas_ActiveWindow(void);

// Make `w` the focused window; nullptr clears focus. Clears
// WFLG_WINDOWACTIVE on the outgoing window and sets it on `w`, then
// redraws both title bars. Idempotent — re-activating the already-
// active window is a no-op. `w` need not be linked to the active
// screen; the router only ever passes a window it found there.
void Leargas_SetActiveWindow(struct Window *w);

// Hit-test `screen`'s window list front-to-back. Returns the topmost
// window whose outer rectangle (LeftEdge/TopEdge .. +Width/+Height,
// right/bottom exclusive) contains the screen-space point (x, y), or
// nullptr if the point is over no window / `screen` is null.
[[nodiscard]] struct Window *Leargas_Window_HitTest(struct Screen *screen, i32 x, i32 y);

// Test-only: clear the process-wide active-window pointer without
// touching the (possibly out-of-scope) previously-active window.
// Production focus changes go through Leargas_SetActiveWindow; this
// exists so host unit tests start each scenario from a known state,
// mirroring Leargas_Input_Reset / Leargas_Screen_SetActive(nullptr).
void Leargas_Focus_Reset(void);

// ---- LF — Keyboard event routing ------------------------------------------
//
// IECLASS_RAWKEY events drained from the L0 ring become IDCMP_RAWKEY
// IntuiMessages on the *focused* window's UserPort. Each window that
// requested IDCMP events gets a per-window KOBJ_MSGPORT UserPort at
// OpenWindow time (Phase 3 wraps this behind OpenWindow's IDCMP_*
// flags + ModifyIDCMP). A client WaitPort()s its UserPort and GetMsg()s
// the IntuiMessages, exactly as on V36+.
//
// Capacity of a window's IDCMP ring. Power of two; human keystroke
// rates leave ample headroom between a client's drains.
constexpr u32 LEARGAS_IDCMP_RING_CAP = 16;

// Fill an IntuiMessage from a focused window and a flat input event.
// Pure / dual-target (no allocation, no IPC) so the translation is
// host-testable. Sets ExecMessage.mn_Length, Class (mapped from
// ev->ie_class — IECLASS_RAWKEY → IDCMP_RAWKEY), Code, Qualifier,
// IAddress (nullptr), window-relative MouseX/MouseY (read through
// w->WScreen->MouseX/Y minus the window origin), the Seconds/Micros
// split of ev->ie_ts_ns, and IDCMPWindow = w. Does not touch
// mn_ReplyPort / mn_Node (the delivery path owns those). No-op on
// null arguments.
void Leargas_BuildIntuiMessage(struct IntuiMessage *im, struct Window *w,
                               const struct LeargasInputEvent *ev);

// Key-routing hook. The router (Leargas_Input_Drain) calls the
// installed function for each IECLASS_RAWKEY event with the currently
// focused window (may be nullptr). MsgPort allocation + PutMsg are
// kernel-only, so the kernel installs Leargas_IDCMP_RouteKey at boot;
// host builds (and the dual-target router unit test) leave the hook
// unset, preserving the pre-LF "RAWKEY dropped" behaviour. The hook
// returns true when the event was delivered.
typedef bool (*Leargas_KeyRouteFn)(struct Window *focused, const struct LeargasInputEvent *ev);
void Leargas_SetKeyRouter(Leargas_KeyRouteFn fn);

// Kernel-only: the default key-routing hook. Allocates a struct
// IntuiMessage, fills it via Leargas_BuildIntuiMessage, and PutMsg's
// it to `focused`->UserPort. Returns false (delivering nothing) when
// `focused` is null, has no UserPort, doesn't list IDCMP_RAWKEY in
// IDCMPFlags, or the ring is full (in which case the message is
// freed). Install via Leargas_SetKeyRouter.
[[nodiscard]] bool Leargas_IDCMP_RouteKey(struct Window *focused,
                                          const struct LeargasInputEvent *ev);

// Kernel-only consumer side, mirroring GetMsg/ReplyMsg. GetMsg
// dequeues the next IntuiMessage from `w`'s UserPort (returns false +
// leaves *out untouched when the port is empty or absent); DisposeMsg
// frees a message the client is done with. Phase 1 has the consumer
// free the message directly; Phase 3 replaces DisposeMsg with the
// V36+ ReplyMsg round-trip.
[[nodiscard]] bool Leargas_IDCMP_GetMsg(struct Window *w, struct IntuiMessage **out);
void Leargas_IDCMP_DisposeMsg(struct IntuiMessage *im);

// ---- LG — Gadget framework ------------------------------------------------
//
// Gadgets are plain V36+ `struct Gadget` the client (Clar) allocates and
// chains onto a window via Leargas_AddGadget. Coordinates are
// window-relative. The router hit-tests the chain on a left-button-down
// inside the focused window and gives the hit gadget the pressed look
// (GFLG_SELECTED) + input focus (the active gadget); the up-stroke
// clears the pressed look. Phase 1 LG renders a button-style face +
// GadgetText label and tracks selection; IDCMP_GADGETUP / GADGETDOWN
// delivery and string editing land in LH.
//
// All dual-target (pure list ops + Dath rendering) — host-testable.

// Append `g` to the end of `w`'s FirstGadget chain. No-op on null args
// or if `g` is already linked (its NextGadget is left intact otherwise).
void Leargas_AddGadget(struct Window *w, struct Gadget *g);

// Unlink `g` from `w`'s FirstGadget chain. Safe if `g` was never linked.
void Leargas_RemoveGadget(struct Window *w, struct Gadget *g);

// Hit-test `w`'s gadget chain at the window-relative point (wx, wy).
// Returns the first gadget whose box contains the point, skipping
// GFLG_DISABLED gadgets; nullptr if none / `w` is null.
[[nodiscard]] struct Gadget *Leargas_Gadget_HitTest(struct Window *w, i32 wx, i32 wy);

// Render gadget `g` into `w`'s screen framebuffer: a bordered button
// face (a pressed shade when GFLG_SELECTED, ghosted when GFLG_DISABLED),
// plus its GadgetText label. Window-relative coordinates are offset by
// the window origin. No-op when the window has no backing framebuffer.
void Leargas_Gadget_Render(struct Window *w, struct Gadget *g);

// Render every gadget in `w`'s FirstGadget chain. Called by
// Leargas_Window_Render after the window chrome so gadgets sit on top.
void Leargas_Window_RenderGadgets(struct Window *w);

// The gadget currently holding input focus (the last one pressed), or
// nullptr. LH routes keystrokes to the active gadget when it is a string
// Inntin. SetActiveGadget(nullptr) clears focus.
[[nodiscard]] struct Gadget *Leargas_ActiveGadget(void);
void Leargas_SetActiveGadget(struct Gadget *g);

// Test-only: clear the active-gadget pointer without side effects,
// mirroring Leargas_Focus_Reset.
void Leargas_Gadget_Reset(void);

#endif // CARA_LEARGAS_H
