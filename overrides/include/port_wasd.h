// The launcher's "W A S D move the camera" setting: the four letter keys as the keys of the camera table that move the view.
// Only in the port; off, the key routing is the Mac's.
#pragma once
#include <cstdint>

#include "TPCamera.h"
#include "TPMain.h"
#include "keyboard.h"
#include "PortFreeCamera.h"

// The launcher sets it from its Settings box (launcher/main.cpp); off by default, and the differential tests leave it off.
inline bool gPortWasdCamera = false;
// The camera table's key each of the four (W, A, S, D) last pressed, 0 for none, so its key up lets go of that key even when the
// view changed in between.
inline int16_t gPortWasdHeld[4] = {};

// The original camera table codes for up, left, down and right.
inline int16_t PortMovementCameraKey(unsigned direction) {
    static constexpr int16_t kParkKeys[4] = {104, 100, 98, 102};             // keypad 8, 4, 2, 6
    static constexpr int16_t kWalkKeys[4] = {0x2600, 0x2500, 0x2800, 0x2700};   // up, left, down, right
    return (gFirstPersonFlags & kFirstPersonAny) != 0 ? kWalkKeys[direction] : kParkKeys[direction];
}

// Which of the four (0 W, 1 A, 2 S, 3 D) a key of the key tables is, by its place on the keyboard (Z Q S D on AZERTY); -1 for
// any other key, and with gPortWasdCamera off.
inline int PortWasdIndex(int32_t key) {
    if (!gPortWasdCamera) return -1;
    // The Mac key codes of the places of W, A, S and D on a US keyboard, and the letters there on the host's layout (the
    // differential tests set no layout: the US letters).
    static constexpr uint8_t kPlaces[4] = {0x0d, 0x00, 0x01, 0x02};
    static constexpr char kUSLetters[4] = {'W', 'A', 'S', 'D'};
    for (int i = 0; i < 4; ++i) {
        const char letter = gPortPlainKey != nullptr ? gPortPlainKey(kPlaces[i]) : kUSLetters[i];
        if (letter != 0 && static_cast<int16_t>(key) == letter) return i;
    }
    return -1;
}

// A key up of one of the four lets go of the camera key its key down pressed, if it still holds one.
inline void PortWasdLetGo(int32_t key) {
    // freecam: with "Free camera" on, a key up also lets go of what the key held for the free camera, whichever window has
    // the keyboard (a free camera key must never outlive its key)
    if (gPortFreeCamera) PortFreeLetGoKey(PortFreeKeyOf(key));
    const int index = PortWasdIndex(key);
    if (index < 0 || gPortWasdHeld[index] == 0) return;
    gKeyTableCamera->KeyUp(gPortWasdHeld[index], 0);
    gPortWasdHeld[index] = 0;
}

// TPW_PORT_FIX wasd-camera gPortWasdCamera/PortWasdCameraKey | Mac: no such setting | Port: with gPortWasdCamera on, the plain (no modifier) W, A, S and D, by their place on the keyboard (Z Q S D on AZERTY), go to the camera table as keypad 8, 4, 2 and 6 over the park (scrolling it) and as the up, left, down and right arrows in first person (walking and turning); true when the key was one of the four, and then the caller's own tables are not asked; a key up lets go of what its key down pressed whatever modifier is held by then | Why: to move the map with W A S D instead of the mouse, as the maintainer asked
inline bool PortWasdCameraKey(int32_t key, int32_t modifiers, bool down) {
    // freecam: the free camera's keys first (PortFreeCameraKey, include/PortFreeCamera.h, which says what it takes)
    if (PortFreeCameraKey(key, modifiers, down)) return true;
    const int index = PortWasdIndex(key);
    if (index < 0) return false;
    if (!down) PortWasdLetGo(key);
    if (modifiers != 0) return false;
    if (down) {
        const int16_t code = PortMovementCameraKey(index);
        int16_t& held = gPortWasdHeld[index];
        if (held != 0 && held != code) gKeyTableCamera->KeyUp(held, 0);
        gKeyTableCamera->KeyDown(code, 0);
        held = code;
    }
    return true;
}

// TPW_PORT_FIX wasd-camera PortWasdNothing | Mac: no such setting | Port: with gPortWasdCamera on, a window where the arrows do not move the camera (a panel) takes the plain four and does nothing with them, but lets go of a camera key one of them still holds | Why: so the four letters never open a screen while the setting is on, and a key held while the panel opened does not keep the camera moving
inline bool PortWasdNothing(int32_t key, int32_t modifiers) {
    PortWasdLetGo(key);
    return modifiers == 0 && PortWasdIndex(key) >= 0;
}
// TPW_PORT_FIX wasd-stuck PortWasdLetGoAll | Mac: no such setting | Port: with gPortWasdCamera on, every camera key the four still hold is let go when the game loses the focus (ThemeParkWorld::HandleActivateWindow) and when the park's window goes (Engine_GameWinMsg) | Why: a camera key must never outlive its key; one left held kept the camera scrolling through a trip to the lobby (reported by the maintainer)
inline void PortWasdLetGoAll() {
    PortFreeLetGoAll();   // freecam: and every key the free camera holds
    PortFreePadLetGoButtons();   // gamepad: and every camera key a gamepad's button holds
    for (int16_t& held : gPortWasdHeld) {
        if (held == 0) continue;
        gKeyTableCamera->KeyUp(held, 0);
        held = 0;
    }
}
// TPW_PORT_FIX wasd-diagonal PortWasdPlace | Mac: no such setting | Port: with gPortWasdCamera on, the run loop's tpw::gPortKeyTogether (platform/gamework.cpp): whether a Mac key code is one of the four places (W A S D on a US keyboard), which then stay down together so that two of them scroll the park diagonally | Why: the camera reads its table's keys every frame, so keypad 8 with keypad 4 held together already scroll diagonally; only the run loop's one key at a time kept the two from being down at once (asked for by the maintainer)
inline bool PortWasdPlace(uint8_t macKeyCode) {
    static constexpr uint8_t kPlaces[4] = {0x0d, 0x00, 0x01, 0x02};
    for (const uint8_t place : kPlaces) {
        if (macKeyCode == place) return true;
    }
    return false;
}
// TPW_PORT_FIX wasd-stuck PortWasdStrayKeyUp | Mac: no such setting | Port: with gPortWasdCamera on, the run loop (tpw::gPortStrayKeyUp, platform/gamework.cpp) passes the key-up of a key it had no longer remembered, by its Mac key code; one of the four places (W A S D on a US keyboard) lets go of the camera key it still holds | Why: such a key-up reached no window, and the camera key its key down pressed stayed down (reported by the maintainer)
inline void PortWasdStrayKeyUp(uint8_t macKeyCode) {
    PortFreeStrayKeyUp(macKeyCode);   // freecam: and what the key holds for the free camera
    static constexpr uint8_t kPlaces[4] = {0x0d, 0x00, 0x01, 0x02};
    for (int i = 0; i < 4; ++i) {
        if (macKeyCode != kPlaces[i] || gPortWasdHeld[i] == 0) continue;
        gKeyTableCamera->KeyUp(gPortWasdHeld[i], 0);
        gPortWasdHeld[i] = 0;
    }
}
// TPW_PORT_FIX freecam PortCameraKeysTogether | Mac: (none) | Port: the camera's keys that stay down together (PortKeyTogetherFor asks it): the four of W A S D (with that setting on) and, while "Free camera" is on, the free camera's keys (PortFreePlace) | Why: Z with Q, or W with Shift and Q in flight, are held at once
inline bool PortCameraKeysTogether(uint8_t macKeyCode) {
    return (gPortWasdCamera && PortWasdPlace(macKeyCode)) || PortFreePlace(macKeyCode);
}
// TPW_PORT_FIX typing-order PortKeyTogetherFor | Mac: no such settings: the key that is down is released before the next one goes down | Port: the launcher's PortKeyTogether (tpw::gPortKeyTogether, platform/gamework.cpp) with whether an edit box has the keyboard and the "Several keys at once" setting: no key while an edit box has it, asked first; otherwise every key with "Several keys at once", and the camera's keys (PortCameraKeysTogether: W A S D with that setting, the free camera's with "Free camera") | Why: with "Free camera" on the launcher gave the run loop the camera's keys alone, with no edit box test, so a name typed with it on still came out in the order the keys came up ("sd" typed quickly as "ds")
inline bool PortKeyTogetherFor(uint8_t macKeyCode, bool editBox, bool severalKeys) {
    if (editBox) return false;
    return severalKeys || PortCameraKeysTogether(macKeyCode);
}
