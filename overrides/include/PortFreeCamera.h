// The launcher's "Free camera" (the port's fix freecam): a camera that tilts freely, zooms further, flies, and takes a
// trackpad's swipes and pinches. Only in the port, and only with gPortFreeCamera (include/TPCamera.h) on; off, every line
// of the original camera runs unchanged. The input side is here, header only, so the host test (tests/host_free_camera.cpp)
// reaches it; the camera side is in HandleCamera (src/game/TPCamera.cpp), the pointer's side in TPMain.cpp and the run
// loop's in platform/gamework.cpp.
#pragma once
#include <cmath>
#include <cstdint>

#include "TPCamera.h"
#include "TPMain.h"
#include "keyboard.h"

// TPW_PORT_FIX freecam settings | Mac: (none) | Port: the launcher's Free camera sub-settings, set before the game starts (launcher/main.cpp); the defaults are the launcher's | Why: the Free camera's options, as SPEC asks
inline bool gPortFreeSwipeZooms = true;           // Two-finger swipe: true "Zooms and rotates", false "Moves the camera"
inline bool gPortFreeInvertLook = false;          // Invert vertical look
inline float gPortFreeLookSensitivity = 1.0f;     // Look sensitivity, 0.25 to 4
inline float gPortFreeFlySpeed = 1.0f;            // Fly speed, 0.25 to 4 (right button + wheel changes it until the game quits)
inline bool gPortFreeSmoothing = true;            // Smoothing: the tilt, the turn and the flight ease
inline bool gPortFreeThroughGround = false;       // Fly through the ground
// The modifiers held now, as the game's modifier byte (3 shift, 0xc command or control, 0x30 option); the launcher sets it
// (the platform's key state). None: no modifier.
inline int32_t (*gPortFreeModifiers)() = nullptr;
// Whether one of the key tables the park's keys go to has an entry for a key with these modifiers (PortFreeHookTableKey, set
// by the launcher), so the free camera leaves that key to them. None: no table has any.
inline bool (*gPortFreeTableKey)(int32_t key, int32_t modifiers) = nullptr;

// The limits. The tilt (gCameraPitch, the angle down from the horizon) goes from just above the horizon to straight down;
// the zoom (gCameraF68, the distance from the target) from 25 to 300.
inline constexpr float kPortFreePitchLow = 0.05f;      // about 3 degrees
inline constexpr float kPortFreePitchHigh = 1.5533f;   // about 89 degrees
inline constexpr float kPortFreeZoomIn = 25.0f;
inline constexpr float kPortFreeZoomOut = 300.0f;
// The eye stays this far above the height field under it. Not less: where the park's ground is a model and not the height
// field (outside the entrance gate), a lower eye sits under that model and sees the sky through it (the live check's
// pictures p2-low-hole and p2-low-fixed).
inline constexpr float kPortFreeGroundMargin = 15.0f;
inline constexpr float kPortFreeTiltRate = 0.0012f;    // Z and X: radians a millisecond
inline constexpr float kPortFreeDragTurn = 0.005f;     // a mouse drag: radians a pixel at sensitivity 1
inline constexpr float kPortFreeSwipeTurn = 0.03f;     // a swipe: radians a unit of SDL's precise delta
inline constexpr float kPortFreeSwipeTilt = 0.02f;
inline constexpr float kPortFreeSwipeMove = 0.02f;     // a swipe: the zoom's share a unit
inline constexpr float kPortFreePinchZoom = 0.85f;     // Ctrl + wheel: the zoom times this a unit up
inline constexpr uint64_t kPortFreeFineMs = 300;       // a wheel event this soon after a fractional one is a trackpad's too
// Fly mode: the eye looks from 85 degrees up to straight down, moves kPortFreeFlySpeed units a millisecond at Fly speed 1, a
// notched wheel step moves it kPortFreeFlyWheelStep along the view, and it stays within kPortFreeFlyMapMargin of the map and
// below kPortFreeFlyCeiling.
inline constexpr float kPortFreeFlyPitchUp = -1.4835299f;
inline constexpr float kPortFreeFlySpeed = 0.06f;
inline constexpr float kPortFreeFlyTurnRate = 0.0015f;   // the left and right arrows: radians a millisecond
inline constexpr float kPortFreeFlyWheelStep = 10.0f;
inline constexpr float kPortFreeFlyPinchMove = 60.0f;    // a pinch: units along the view for a scale of e
inline constexpr float kPortFreeFlyMapMargin = 150.0f;
inline constexpr float kPortFreeFlyCeiling = 500.0f;
inline constexpr float kPortFreeFlySpeedStep = 1.25f;    // right button + a wheel step: the fly speed times this
// Low views: below kPortFreeLowEnter (25 degrees) the park is drawn as first person draws it (out to the View distance, with
// fog, the visitors' first-person sprites), back above kPortFreeLowLeave (30 degrees).
inline constexpr float kPortFreeLowEnter = 0.43633232f;
inline constexpr float kPortFreeLowLeave = 0.52359878f;

// Which key of the free camera's a key of the key tables is: the letters by their place on the keyboard (as W A S D,
// include/port_wasd.h), Tab, Page Up and Page Down by their codes (the original's key table, platform/key_tables.inc, gives the
// Mac's Page Up key Windows' VK_NEXT, 0x2200 here, and its Page Down VK_PRIOR, 0x2100).
enum PortFreeKeyId : int {
    kPortFreeQ, kPortFreeE, kPortFreeZ, kPortFreeX, kPortFreeW, kPortFreeA, kPortFreeS, kPortFreeD,
    kPortFreeTab, kPortFreePgUp, kPortFreePgDn, kPortFreeKeyCount
};
inline constexpr uint8_t kPortFreePlaces[kPortFreeKeyCount] = {0x0c, 0x0e, 0x06, 0x07, 0x0d, 0x00, 0x01, 0x02, 0x30, 0x74, 0x79};

// The free camera's state. pitchTarget below 0: not yet set (HandleCamera starts it at the original's tilt for the zoom).
struct PortFreeCameraState {
    float pitchTarget = -1.0f;
    uint32_t held = 0;                       // bit (1 << PortFreeKeyId) of each key the free camera holds
    int16_t tableHeld[kPortFreeKeyCount] = {};   // the camera table's key a key pressed (Q, E, Page Up, Page Down in orbit)
    // What the wheel, the drags and the pinch asked for since the last frame; HandleCamera takes it.
    float dYaw = 0.0f;       // radians, added to the key yaw (larger turns the view right)
    float dPitch = 0.0f;     // radians, added to the tilt (larger looks further down)
    float dZoom = 0.0f;      // zoom units, added
    float zoomScale = 1.0f;  // the zoom times this
    float dForward = 0.0f;   // moves, in shares of the zoom, along the view's ground direction
    float dRight = 0.0f;
    bool haveFine = false;   // a fractional wheel event was seen, at lastFineMs
    uint64_t lastFineMs = 0;
    int dragButton = -1;     // the button of the drag the run loop holds in relative mouse mode (0 left, 1 right, 2 middle)
    // Fly mode. Tab sets flyWanted; HandleCamera starts and ends the flight (fly) from it.
    bool flyWanted = false;
    bool fly = false;
    float eye[3] = {};       // the eye
    float flyPitch = 0.0f;   // the view: down from the horizon (below 0 looks up), and the yaw as gCameraYaw
    float flyYaw = 0.0f;
    float flyPitchTarget = 0.0f;
    float flyYawTarget = 0.0f;
    float flyVelocity[3] = {};   // units a millisecond
    float flyAhead = 0.0f;       // units still to move along the view (the wheel, the pinch)
    float flySpeedLive = 1.0f;   // right button + wheel: the Fly speed setting times this, until the game quits
    bool lowView = false;        // drawn as a low view (kPortFreeLowEnter, kPortFreeLowLeave)
};
inline PortFreeCameraState gPortFree;

// The game's modifier byte held now.
inline int32_t PortFreeModifiersNow() { return gPortFreeModifiers != nullptr ? gPortFreeModifiers() : 0; }
// Shift goes three times as fast, Option a third as fast.
inline float PortFreeSpeedFactor(int32_t modifiers) {
    float factor = 1.0f;
    if ((modifiers & 3) != 0) factor *= 3.0f;
    if ((modifiers & 0x30) != 0) factor /= 3.0f;
    return factor;
}

// The original's tilt for a zoom (HandleCamera: a quarter turn's half at 70, 1.134464 at 130, within its limits), within
// the free camera's.
inline float PortFreeFormulaPitch(float zoom) {
    const float t = (zoom - 70.0f) / 60.0f;
    float pitch = 0.7853982f * (1.0f - t) + 1.134464f * t;
    if (pitch < 0.0f) pitch = 0.0f;
    if (pitch > 1.4707963f) pitch = 1.4707963f;
    return std::fmin(std::fmax(pitch, kPortFreePitchLow), kPortFreePitchHigh);
}
inline float PortFreeClampPitch(float pitch) { return std::fmin(std::fmax(pitch, kPortFreePitchLow), kPortFreePitchHigh); }

// One frame of dt ms of easing toward a target, at the rate the original eases the yaw (dt / 140 of the way, at most all of
// it); with Smoothing off, the target at once.
inline float PortFreeEase(float value, float target, float dt) {
    if (!gPortFreeSmoothing) return target;
    const float share = std::fmin(dt, 140.0f) / 140.0f;
    const float next = value * (1.0f - share) + target * share;
    return std::fabs(next - target) < 5e-4f ? target : next;
}

// The tilt that keeps the eye at least kPortFreeGroundMargin above the ground under it: the eye is the zoom away from the
// target at (tx, base, tz), tilt pitch down toward it, turned by yaw; ground(x, z) is the ground's height. Raises the tilt
// (three passes, as the eye moves when it rises) and never lowers it.
template <class Ground>
inline float PortFreeFloorPitch(float pitch, float yaw, float zoom, float tx, float base, float tz, Ground ground) {
    if (!(zoom > 0.0f)) return pitch;
    const float s = std::sin(yaw);
    const float c = std::cos(yaw);
    for (int pass = 0; pass < 3; ++pass) {
        const float across = zoom * std::cos(pitch);
        const float ex = tx - across * s;
        const float ez = tz - across * c;
        const float need = (ground(ex, ez) + kPortFreeGroundMargin - base) / zoom;
        if (zoom * std::sin(pitch) >= need * zoom) break;
        pitch = need >= 1.0f ? 1.5707963f : std::asin(need);
    }
    return pitch;
}

// The low view's hysteresis: whether the view at this pitch is drawn as a low one, given whether the last frame's was.
inline bool PortFreeLowViewAt(float pitch, bool wasLow) {
    if (pitch < kPortFreeLowEnter) return true;
    if (pitch > kPortFreeLowLeave) return false;
    return wasLow;
}

// The zoom a save holds (the original's 70 to 130) and the key yaw it holds with Isometric on (the nearest quarter turn, as
// the original's first isometric frame makes it).
inline float PortFreeSaveZoom(float zoom) { return std::fmin(std::fmax(zoom, 70.0f), 130.0f); }
inline float PortFreeIsoYaw(float yaw) {
    const float quarter = 1.5707964f;
    const float y = static_cast<float>(std::fmod(static_cast<double>(yaw), 1.5707963267948966));
    if (!(y < 0.0f)) return y > 0.7853982f ? yaw + (quarter - y) : yaw - y;
    return !(y < -0.7853982f) ? yaw - y : yaw - (quarter + y);
}

// One frame of fly mode's move, dt ms: the velocity (units a millisecond) eases toward the keys' wish, given as forward,
// right and up shares (-1 to 1 each) along the view's forward and right rows and the world's up, at speed units a
// millisecond; returns the move (velocity times dt plus the share of flyAhead taken this frame).
inline void PortFreeFlyMove(const float forward[3], const float right[3], float wantForward, float wantRight, float wantUp,
                            float speed, float dt, float move[3]) {
    float want[3];
    for (int i = 0; i < 3; ++i) want[i] = (wantForward * forward[i] + wantRight * right[i]) * speed;
    want[1] += wantUp * speed;
    for (int i = 0; i < 3; ++i) gPortFree.flyVelocity[i] = PortFreeEase(gPortFree.flyVelocity[i], want[i], dt);
    const float aheadLeft = PortFreeEase(gPortFree.flyAhead, 0.0f, dt);
    const float ahead = gPortFree.flyAhead - aheadLeft;
    gPortFree.flyAhead = aheadLeft;
    for (int i = 0; i < 3; ++i) move[i] = gPortFree.flyVelocity[i] * dt + ahead * forward[i];
}

// ---- keys

inline int PortFreeKeyOf(int32_t key) {
    static constexpr char kUSLetters[8] = {'Q', 'E', 'Z', 'X', 'W', 'A', 'S', 'D'};
    for (int i = 0; i < 8; ++i) {
        const char letter = gPortPlainKey != nullptr ? gPortPlainKey(kPortFreePlaces[i]) : kUSLetters[i];
        if (letter != 0 && static_cast<int16_t>(key) == letter) return i;
    }
    if (key == 9) return kPortFreeTab;
    if (key == 0x2200) return kPortFreePgUp;
    if (key == 0x2100) return kPortFreePgDn;
    return -1;
}

// Lets go of what one of the keys holds: its bit, and the camera table's key it pressed. True when it held something.
inline bool PortFreeLetGoKey(int which) {
    if (which < 0) return false;
    bool held = (gPortFree.held & (1u << which)) != 0;
    gPortFree.held &= ~(1u << which);
    if (gPortFree.tableHeld[which] != 0) {
        gKeyTableCamera->KeyUp(gPortFree.tableHeld[which], 0);
        gPortFree.tableHeld[which] = 0;
        held = true;
    }
    return held;
}
inline void PortFreeLetGoAll() {
    for (int i = 0; i < kPortFreeKeyCount; ++i) PortFreeLetGoKey(i);
}
// Whether the free camera flies (or Tab has just asked it to): its keys, wheel and drags are fly mode's.
inline bool PortFreeFlying() { return gPortFreeCamera && (gPortFree.flyWanted || gPortFree.fly); }
// TPW_PORT_FIX freecam PortFreeHearsFromEye | Mac: (none: outside first person the ear is halfway to what the camera looks at) | Port: with gPortFreeCamera on and the camera flying, the ear is at the eye, as in first person (GameSys.cpp) | Why: sound heard from the eye in fly mode, as SPEC asks
inline bool PortFreeHearsFromEye() { return gPortFreeCamera && gPortFree.fly; }

// TPW_PORT_FIX freecam PortFreeCameraKey | Mac: (none) | Port: with gPortFreeCamera on, over the park (not in first person): Tab starts and ends fly mode; in the orbit, Q and E press the camera table's turn keys (the left and right arrows, so the Isometric option's quarter-turn snap holds), Page Up and Page Down its zoom keys (the up arrow, zoom in, and the down arrow, zoom out), and Z and X tilt (held bits HandleCamera reads); in fly mode W A S D, Q and E, Z and X, Page Up and Page Down are all held bits HandleCamera reads (W A S D whether or not "W A S D move the camera" is on); Shift and Option may be held (faster, slower), Command and Control leave the key to the tables, and so does a key with modifiers one of the tables has (Option-S, the quick save); the key up lets go of what its key down took; true when the key was taken | Why: the Free camera's keys, as SPEC asks
inline bool PortFreeCameraKey(int32_t key, int32_t modifiers, bool down) {
    if (!gPortFreeCamera) return false;
    const int which = PortFreeKeyOf(key);
    if (which < 0) return false;
    if (!down) return PortFreeLetGoKey(which);
    if ((gFirstPersonFlags & kFirstPersonAny) != 0 || (modifiers & 0xc) != 0) return false;
    if (modifiers != 0 && gPortFreeTableKey != nullptr && gPortFreeTableKey(key, modifiers)) return false;
    if (which == kPortFreeTab) {
        // The game's own key repeat sends more key downs: only the first toggles.
        if ((gPortFree.held & (1u << kPortFreeTab)) == 0) {
            for (int i = 0; i < kPortFreeKeyCount; ++i) {
                if (i != kPortFreeTab) PortFreeLetGoKey(i);
            }
            gPortFree.flyWanted = !PortFreeFlying();
        }
        gPortFree.held |= 1u << kPortFreeTab;
        return true;
    }
    if (PortFreeFlying()) {   // every one of the keys is fly mode's: held bits HandleCamera reads
        gPortFree.held |= 1u << which;
        return true;
    }
    switch (which) {
    case kPortFreeZ:
    case kPortFreeX:
        gPortFree.held |= 1u << which;
        return true;
    case kPortFreeQ:
    case kPortFreeE:
    case kPortFreePgUp:
    case kPortFreePgDn: {
        static constexpr int16_t kCodes[4] = {0x2500, 0x2700, 0x2600, 0x2800};   // left, right, up (zoom in), down (zoom out)
        const int16_t code = kCodes[which == kPortFreeQ ? 0 : which == kPortFreeE ? 1 : which == kPortFreePgUp ? 2 : 3];
        int16_t& held = gPortFree.tableHeld[which];
        if (held != 0 && held != code) gKeyTableCamera->KeyUp(held, 0);
        if (held != code) gKeyTableCamera->KeyDown(code, 0);
        held = code;
        return true;
    }
    default:
        return false;
    }
}

// Whether a key, by its Mac key code, is one of the free camera's (the run loop keeps them down together, as W A S D).
inline bool PortFreePlace(uint8_t macKeyCode) {
    if (!gPortFreeCamera) return false;
    for (const uint8_t place : kPortFreePlaces) {
        if (macKeyCode == place) return true;
    }
    return false;
}
// A key-up the run loop could not match, by its Mac key code: lets go of what that key holds.
inline void PortFreeStrayKeyUp(uint8_t macKeyCode) {
    for (int i = 0; i < kPortFreeKeyCount; ++i) {
        if (macKeyCode == kPortFreePlaces[i]) PortFreeLetGoKey(i);
    }
}

// ---- the wheel, the drags, the pinch

// TPW_PORT_FIX freecam PortFreeWheel | Mac: (none: a wheel step zooms by 10) | Port: with gPortFreeCamera on, over the park and not in first person: a trackpad (a fractional delta, or any wheel event within kPortFreeFineMs of one) zooms and turns ("Zooms and rotates", the default: sideways turns, and up and down is left to the original, which zooms by its whole steps, 10 each, as with the setting off) or moves the camera ("Moves the camera": up and down forward and back, sideways sideways), and with Option tilts and turns; Control or Command with the wheel is a pinch (Windows' precision touchpads send their pinch so) and zooms; the deltas are SDL's as they come, which follow the system's natural scrolling; a notched wheel is not taken (the original's 10 a step); in fly mode a notched wheel and the pinch move along the view, a swipe looks, and with the right button held the wheel sets the fly speed (a quarter to four times the setting); true when taken | Why: the Free camera's trackpad, as SPEC asks; a swipe zooms by default, as it did with the setting off, since with a swipe that moved the camera the normal camera had lost its two-finger zoom (the maintainer's 0.5.6 test)
inline bool PortFreeWheel(float x, float y, bool fractional, int32_t modifiers, uint64_t nowMs, bool overPark) {
    if (!gPortFreeCamera) return false;
    if (fractional) {
        gPortFree.haveFine = true;
        gPortFree.lastFineMs = nowMs;
    }
    if (!overPark || (gFirstPersonFlags & kFirstPersonAny) != 0) return false;
    const bool fine = fractional || (gPortFree.haveFine && nowMs >= gPortFree.lastFineMs && nowMs - gPortFree.lastFineMs < kPortFreeFineMs);
    if (PortFreeFlying()) {
        if (gPortFree.dragButton == 1) {   // right button held: the fly speed
            gPortFree.flySpeedLive = std::fmin(std::fmax(gPortFree.flySpeedLive * std::pow(kPortFreeFlySpeedStep, y), 0.25f), 4.0f);
        } else if ((modifiers & 0xc) != 0 || !fine) {   // a pinch (Windows) or a notched wheel: along the view
            gPortFree.flyAhead += y * kPortFreeFlyWheelStep;
        } else {   // a swipe looks
            const float sense = gPortFreeLookSensitivity;
            gPortFree.dYaw += x * kPortFreeSwipeTurn * sense;
            gPortFree.dPitch -= y * kPortFreeSwipeTilt * sense * (gPortFreeInvertLook ? -1.0f : 1.0f);
        }
        return true;
    }
    if ((modifiers & 0xc) != 0) {
        gPortFree.zoomScale *= std::pow(kPortFreePinchZoom, y);
        return true;
    }
    if (!fine) return false;
    const float sense = gPortFreeLookSensitivity;
    const float invert = gPortFreeInvertLook ? -1.0f : 1.0f;
    if ((modifiers & 0x30) != 0) {
        gPortFree.dYaw += x * kPortFreeSwipeTurn * sense;
        gPortFree.dPitch -= y * kPortFreeSwipeTilt * sense * invert;
    } else if (gPortFreeSwipeZooms) {
        gPortFree.dYaw += x * kPortFreeSwipeTurn * sense;
        return false;   // up and down: the original's zoom
    } else {
        gPortFree.dForward += y * kPortFreeSwipeMove;
        gPortFree.dRight += x * kPortFreeSwipeMove;
    }
    return true;
}

// TPW_PORT_FIX freecam PortFreeDragStart | Mac: (none: the middle button turned the view by the pointer's distance from the middle of the screen) | Port: with gPortFreeCamera on, over the park and not in first person, the middle button, or the left with Option, starts a drag the run loop holds in relative mouse mode: sideways turns, up and down tilts (in fly mode: looks), by how far it moves; in fly mode the right button too, whose quick click still goes to the game (2); 0 when not taken | Why: the Free camera's mouse, as SPEC asks
inline int PortFreeDragStart(int button, int32_t modifiers, bool overPark) {
    if (!gPortFreeCamera || !overPark || (gFirstPersonFlags & kFirstPersonAny) != 0) return 0;
    if (button == 2 || (button == 0 && (modifiers & 0x30) != 0)) {
        gPortFree.dragButton = button;
        return 1;
    }
    if (button == 1 && PortFreeFlying()) {
        gPortFree.dragButton = button;
        return 2;
    }
    return 0;
}
inline void PortFreeDragMove(float dx, float dy) {
    if (gPortFree.dragButton < 0) return;
    const float step = kPortFreeDragTurn * gPortFreeLookSensitivity;
    gPortFree.dYaw += dx * step;
    gPortFree.dPitch += dy * step * (gPortFreeInvertLook ? -1.0f : 1.0f);
}
// Whether the drag may go on: not once first person starts or a panel opens.
inline bool PortFreeDragGoing(bool panelOpen) {
    return gPortFreeCamera && !panelOpen && (gFirstPersonFlags & kFirstPersonAny) == 0;
}
inline void PortFreeDragEnd() { gPortFree.dragButton = -1; }

// A pinch's scale change since the last (SDL 3.4's SDL_EVENT_PINCH_UPDATE): above 1 zooms in.
// In fly mode it moves along the view.
inline void PortFreePinch(float scale, bool overPark) {
    if (!gPortFreeCamera || !overPark || (gFirstPersonFlags & kFirstPersonAny) != 0 || !(scale > 0.0f)) return;
    if (PortFreeFlying()) gPortFree.flyAhead += std::log(scale) * kPortFreeFlyPinchMove;
    else gPortFree.zoomScale /= scale;
}

// Clears what the input asked for (HandleCamera took it, or first person dropped it).
inline void PortFreeClearPending() {
    gPortFree.dYaw = gPortFree.dPitch = gPortFree.dZoom = gPortFree.dForward = gPortFree.dRight = 0.0f;
    gPortFree.zoomScale = 1.0f;
}

// The Mac's modifier word (0x200 shift, 0x100 command, 0x1000 control, 0x800 option) as the game's byte, as HandleTyping
// makes it.
inline int32_t PortFreeGameModifiers(uint16_t mac) {
    int32_t mods = 0;
    if ((mac & 0x200) != 0) mods |= 3;
    if ((mac & 0x1100) != 0) mods |= 0xc;
    if ((mac & 0x800) != 0) mods |= 0x30;
    return mods;
}

// The run loop's hooks (platform/platform.h tpw::gPortWheel and the rest), which the launcher sets with the setting on;
// in src/game/TPMain.cpp, which knows whether the pointer is over the park.
bool PortFreeOverPark();
bool PortFreeHookWheel(float x, float y, bool fractional, uint16_t macModifiers, uint64_t nowMs);
int PortFreeHookDragStart(int button, uint16_t macModifiers);
void PortFreeHookDragMove(float dx, float dy);
bool PortFreeHookDragGoing();
void PortFreeHookDragEnd();
void PortFreeHookPinch(float scale);
bool PortFreeHookTableKey(int32_t key, int32_t modifiers);

// TPW_PORT_FIX gamepad PortFreePadFrame | Mac: (none: the Mac version took no gamepad) | Port: with gPortFreeCamera and the launcher's "Gamepad" (gPortFreePadOn) on, a gamepad in SDL's standard layout is free camera input, over the park and not in first person: its sticks and triggers are kept as levels (PortFreePadAxis) and each frame turned into dt ms of the deltas the wheel and the drags write (PortFreePadFrame: in the orbit the left stick moves, the right stick turns and tilts, the right trigger zooms in and the left out; in fly mode the left stick flies along the view and sideways, the right stick looks, the right trigger rises and the left sinks), past a dead zone and on a squared curve, at the Look sensitivity, Invert vertical look and Fly speed settings; its buttons press what the keys press (PortFreePadButton: in the orbit the shoulders the camera table's turn keys, Y starts and ends fly mode as Tab does, Back resets the view as Home does) | Why: the Free camera from a gamepad, with no new camera code; off, or with no pad, nothing changes
// The launcher's "Gamepad" row, under Free camera: on, the run loop opens gamepads and sends them here.
inline bool gPortFreePadOn = false;
// SDL's standard gamepad layout (SDL_GamepadAxis, SDL_GamepadButton; launcher/main.cpp checks the numbers).
enum PortFreePadAxisId : int {
    kPortFreePadLeftX, kPortFreePadLeftY, kPortFreePadRightX, kPortFreePadRightY, kPortFreePadLeftTrigger,
    kPortFreePadRightTrigger, kPortFreePadAxisCount
};
inline constexpr int kPortFreePadNorth = 3;           // Y on an Xbox pad, the triangle on a PlayStation one: fly mode
inline constexpr int kPortFreePadBack = 4;            // Back, View, Share or Select: the reset
inline constexpr int kPortFreePadLeftShoulder = 9;    // turn left
inline constexpr int kPortFreePadRightShoulder = 10;  // turn right
// A stick moves nothing within kPortFreePadDeadZone of the middle (of 32767, SDL's usual dead zone), a trigger nothing
// below kPortFreePadTriggerZone (30 of 255, XInput's trigger threshold); past them the share rises with the square of the
// way to the end, so a small push is a fine one.
inline constexpr int kPortFreePadDeadZone = 8000;
inline constexpr int kPortFreePadTriggerZone = 3855;
// At full tilt, a millisecond: the right stick turns kPortFreePadTurn and tilts kPortFreePadTilt radians at Look
// sensitivity 1, the left stick moves the orbit kPortFreePadMove of the zoom, a trigger zooms by e to the
// kPortFreePadZoom; in fly mode the left stick and the triggers move at the Fly speed, as W A S D, Page Up and Page Down do.
inline constexpr float kPortFreePadTurn = 0.0025f;
inline constexpr float kPortFreePadTilt = 0.0015f;
inline constexpr float kPortFreePadMove = 0.001f;
inline constexpr float kPortFreePadZoom = 0.0012f;
struct PortFreePadState {
    int16_t axis[kPortFreePadAxisCount] = {};   // the levels, as SDL gives them
    int16_t tableHeld[3] = {};                  // the camera table's key the left shoulder, the right one and Back hold
    bool northHeld = false;
    float fly[3] = {};                          // fly mode's wish this frame: forward, right and up shares (-1 to 1)
};
inline PortFreePadState gPortFreePad;

// A trigger's share, 0 to 1.
inline float PortFreePadTrigger(int16_t level) {
    if (level <= kPortFreePadTriggerZone) return 0.0f;
    const float way = std::fmin(static_cast<float>(level - kPortFreePadTriggerZone) / (32767.0f - kPortFreePadTriggerZone), 1.0f);
    return way * way;
}
// A stick's shares, -1 to 1 each: the dead zone is a circle, and the curve keeps the stick's direction.
inline void PortFreePadStick(int16_t x, int16_t y, float& sx, float& sy) {
    sx = sy = 0.0f;
    const float fx = static_cast<float>(x);
    const float fy = static_cast<float>(y);
    const float length = std::sqrt(fx * fx + fy * fy);
    if (length <= static_cast<float>(kPortFreePadDeadZone)) return;
    const float way = std::fmin((length - kPortFreePadDeadZone) / (32767.0f - kPortFreePadDeadZone), 1.0f);
    sx = fx / length * way * way;
    sy = fy / length * way * way;
}

// An axis moved (SDL's standard numbering): the level is kept until it moves again.
inline void PortFreePadAxis(int axis, int16_t value) {
    if (!gPortFreeCamera || !gPortFreePadOn || axis < 0 || axis >= kPortFreePadAxisCount) return;
    gPortFreePad.axis[axis] = value;
}

// One frame of dt ms of the gamepad's sticks and triggers, before HandleCamera takes the deltas.
inline void PortFreePadFrame(float dt) {
    gPortFreePad.fly[0] = gPortFreePad.fly[1] = gPortFreePad.fly[2] = 0.0f;
    if (!gPortFreeCamera || !gPortFreePadOn || (gFirstPersonFlags & kFirstPersonAny) != 0) return;
    float lx, ly, rx, ry;
    PortFreePadStick(gPortFreePad.axis[kPortFreePadLeftX], gPortFreePad.axis[kPortFreePadLeftY], lx, ly);
    PortFreePadStick(gPortFreePad.axis[kPortFreePadRightX], gPortFreePad.axis[kPortFreePadRightY], rx, ry);
    const float rise = PortFreePadTrigger(gPortFreePad.axis[kPortFreePadRightTrigger]) -
                       PortFreePadTrigger(gPortFreePad.axis[kPortFreePadLeftTrigger]);
    // The right stick: right turns the view right, down looks further down (as a mouse drag), up toward the horizon.
    const float sense = gPortFreeLookSensitivity * dt;
    if (rx != 0.0f) gPortFree.dYaw += rx * kPortFreePadTurn * sense;
    if (ry != 0.0f) gPortFree.dPitch += ry * kPortFreePadTilt * sense * (gPortFreeInvertLook ? -1.0f : 1.0f);
    // The left stick: up is forward (SDL's stick is below 0 pushed up).
    if (PortFreeFlying()) {
        gPortFreePad.fly[0] = -ly;
        gPortFreePad.fly[1] = lx;
        gPortFreePad.fly[2] = rise;
        return;
    }
    const float move = kPortFreePadMove * dt * PortFreeSpeedFactor(PortFreeModifiersNow());
    if (ly != 0.0f) gPortFree.dForward -= ly * move;
    if (lx != 0.0f) gPortFree.dRight += lx * move;
    if (rise != 0.0f) gPortFree.zoomScale *= std::exp(-rise * kPortFreePadZoom * dt);
}

// Lets go of what one of the buttons holds (0 the left shoulder, 1 the right one, 2 Back).
inline void PortFreePadLetGo(int which) {
    int16_t& held = gPortFreePad.tableHeld[which];
    if (held == 0) return;
    gKeyTableCamera->KeyUp(held, 0);
    held = 0;
}

// A button went down or up (SDL's standard numbering). parkShown: the park's window is up. True when the camera took it.
inline bool PortFreePadButton(int button, bool down, bool parkShown) {
    if (!gPortFreeCamera || !gPortFreePadOn) return false;
    const int which = button == kPortFreePadLeftShoulder ? 0 : button == kPortFreePadRightShoulder ? 1 : button == kPortFreePadBack ? 2 : -1;
    if (button == kPortFreePadNorth) {
        // Fly mode, as Tab: the keys the orbit took are let go first.
        if (!down) {
            gPortFreePad.northHeld = false;
            return true;
        }
        if (!parkShown || (gFirstPersonFlags & kFirstPersonAny) != 0) return false;
        if (!gPortFreePad.northHeld) {
            for (int i = 0; i < kPortFreeKeyCount; ++i) {
                if (i != kPortFreeTab) PortFreeLetGoKey(i);
            }
            PortFreePadLetGo(0);
            PortFreePadLetGo(1);
            gPortFree.flyWanted = !PortFreeFlying();
        }
        gPortFreePad.northHeld = true;
        return true;
    }
    if (which < 0) return false;
    if (!down) {
        PortFreePadLetGo(which);
        return true;
    }
    if (!parkShown || (gFirstPersonFlags & kFirstPersonAny) != 0) return false;
    if (which < 2 && PortFreeFlying()) return false;   // the turn keys are the orbit's; fly mode looks with the right stick
    // The left and right arrows (turn, the Isometric option's quarter turn kept) and Home (the reset).
    static constexpr int16_t kCodes[3] = {0x2500, 0x2700, 0x2400};
    if (gPortFreePad.tableHeld[which] == 0) gKeyTableCamera->KeyDown(kCodes[which], 0);
    gPortFreePad.tableHeld[which] = kCodes[which];
    return true;
}

// Every camera table key the buttons hold comes up (the park's window went, or the pad did).
inline void PortFreePadLetGoButtons() {
    for (int i = 0; i < 3; ++i) PortFreePadLetGo(i);
    gPortFreePad.northHeld = false;
}
// The gamepad went (unplugged, or the game lost the focus): its sticks are let go and its buttons come up.
inline void PortFreePadGone() {
    for (int16_t& level : gPortFreePad.axis) level = 0;
    PortFreePadLetGoButtons();
}

// The run loop's gamepad hooks (platform/platform.h tpw::gPortPadAxis and the rest), in src/game/TPMain.cpp.
void PortFreeHookPadAxis(int axis, int16_t value);
bool PortFreeHookPadButton(int button, bool down);
void PortFreeHookPadGone();
