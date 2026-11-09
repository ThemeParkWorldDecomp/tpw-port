// The launcher's "W A S D move the camera" setting: the four letter keys as the keys of the camera table that move the view.
// Only in the port; off, the key routing is the Mac's.
#pragma once
#include <cstdint>

#include "TPCamera.h"
#include "TPMain.h"
#include "keyboard.h"

// The launcher sets it from its Settings box (launcher/main.cpp); off by default, and the differential tests leave it off.
inline bool gPortWasdCamera = false;
// The camera table's key each of the four (W, A, S, D) last pressed, 0 for none, so its key up lets go of that key even when the
// view changed in between.
inline int16_t gPortWasdHeld[4] = {};

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
    const int index = PortWasdIndex(key);
    if (index < 0 || gPortWasdHeld[index] == 0) return;
    gKeyTableCamera->KeyUp(gPortWasdHeld[index], 0);
    gPortWasdHeld[index] = 0;
}

// TPW_PORT_FIX wasd-camera gPortWasdCamera/PortWasdCameraKey | Mac: no such setting | Port: with gPortWasdCamera on, the plain (no modifier) W, A, S and D, by their place on the keyboard (Z Q S D on AZERTY), go to the camera table as keypad 8, 4, 2 and 6 over the park (scrolling it) and as the up, left, down and right arrows in first person (walking and turning); true when the key was one of the four, and then the caller's own tables are not asked; a key up lets go of what its key down pressed whatever modifier is held by then | Why: to move the map with W A S D instead of the mouse, as the maintainer asked
inline bool PortWasdCameraKey(int32_t key, int32_t modifiers, bool down) {
    const int index = PortWasdIndex(key);
    if (index < 0) return false;
    if (!down) PortWasdLetGo(key);
    if (modifiers != 0) return false;
    if (down) {
        static constexpr int16_t kParkKeys[4] = {104, 100, 98, 102};             // keypad 8, 4, 2, 6: camera ids 13, 15, 14, 16
        static constexpr int16_t kWalkKeys[4] = {0x2600, 0x2500, 0x2800, 0x2700};   // up, left, down, right: ids 0, 6, 1, 7
        const int16_t code = (gFirstPersonFlags & kFirstPersonAny) != 0 ? kWalkKeys[index] : kParkKeys[index];
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
