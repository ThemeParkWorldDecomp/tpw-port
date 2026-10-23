// keys.cpp: keyboard translation. The game takes Mac virtual key codes (HandleTyping) and
// turns them into Windows virtual keys with macdoze's MacToWindowsVirtualKey, and asks for
// key states by Windows virtual key (GetAsyncKeyState). SDL scancodes are mapped to Mac key
// codes here, and macdoze's own two tables do the rest.
#include <SDL3/SDL.h>

#include "platform.h"
#include "platform/macdoze.h"

#include "key_tables.inc"

namespace {
// SDL scancode -> Mac virtual key code (the ANSI keyboard's codes, Inside Macintosh).
struct ScancodeKey {
    SDL_Scancode scancode;
    uint8_t macKey;
};
const ScancodeKey kScancodeKeys[] = {
    {SDL_SCANCODE_A, 0x00}, {SDL_SCANCODE_S, 0x01}, {SDL_SCANCODE_D, 0x02}, {SDL_SCANCODE_F, 0x03},
    {SDL_SCANCODE_H, 0x04}, {SDL_SCANCODE_G, 0x05}, {SDL_SCANCODE_Z, 0x06}, {SDL_SCANCODE_X, 0x07},
    {SDL_SCANCODE_C, 0x08}, {SDL_SCANCODE_V, 0x09}, {SDL_SCANCODE_B, 0x0b}, {SDL_SCANCODE_Q, 0x0c},
    {SDL_SCANCODE_W, 0x0d}, {SDL_SCANCODE_E, 0x0e}, {SDL_SCANCODE_R, 0x0f}, {SDL_SCANCODE_Y, 0x10},
    {SDL_SCANCODE_T, 0x11}, {SDL_SCANCODE_1, 0x12}, {SDL_SCANCODE_2, 0x13}, {SDL_SCANCODE_3, 0x14},
    {SDL_SCANCODE_4, 0x15}, {SDL_SCANCODE_6, 0x16}, {SDL_SCANCODE_5, 0x17}, {SDL_SCANCODE_EQUALS, 0x18},
    {SDL_SCANCODE_9, 0x19}, {SDL_SCANCODE_7, 0x1a}, {SDL_SCANCODE_MINUS, 0x1b}, {SDL_SCANCODE_8, 0x1c},
    {SDL_SCANCODE_0, 0x1d}, {SDL_SCANCODE_RIGHTBRACKET, 0x1e}, {SDL_SCANCODE_O, 0x1f}, {SDL_SCANCODE_U, 0x20},
    {SDL_SCANCODE_LEFTBRACKET, 0x21}, {SDL_SCANCODE_I, 0x22}, {SDL_SCANCODE_P, 0x23}, {SDL_SCANCODE_RETURN, 0x24},
    {SDL_SCANCODE_L, 0x25}, {SDL_SCANCODE_J, 0x26}, {SDL_SCANCODE_APOSTROPHE, 0x27}, {SDL_SCANCODE_K, 0x28},
    {SDL_SCANCODE_SEMICOLON, 0x29}, {SDL_SCANCODE_BACKSLASH, 0x2a}, {SDL_SCANCODE_COMMA, 0x2b},
    {SDL_SCANCODE_SLASH, 0x2c}, {SDL_SCANCODE_N, 0x2d}, {SDL_SCANCODE_M, 0x2e}, {SDL_SCANCODE_PERIOD, 0x2f},
    {SDL_SCANCODE_TAB, 0x30}, {SDL_SCANCODE_SPACE, 0x31}, {SDL_SCANCODE_GRAVE, 0x32},
    {SDL_SCANCODE_BACKSPACE, 0x33}, {SDL_SCANCODE_ESCAPE, 0x35}, {SDL_SCANCODE_LGUI, 0x37},
    {SDL_SCANCODE_RGUI, 0x37}, {SDL_SCANCODE_LSHIFT, 0x38}, {SDL_SCANCODE_CAPSLOCK, 0x39},
    {SDL_SCANCODE_LALT, 0x3a}, {SDL_SCANCODE_LCTRL, 0x3b}, {SDL_SCANCODE_RSHIFT, 0x38},
    {SDL_SCANCODE_RALT, 0x3a}, {SDL_SCANCODE_RCTRL, 0x3b},
    {SDL_SCANCODE_KP_PERIOD, 0x41}, {SDL_SCANCODE_KP_MULTIPLY, 0x43}, {SDL_SCANCODE_KP_PLUS, 0x45},
    {SDL_SCANCODE_NUMLOCKCLEAR, 0x47}, {SDL_SCANCODE_KP_DIVIDE, 0x4b}, {SDL_SCANCODE_KP_ENTER, 0x4c},
    {SDL_SCANCODE_KP_MINUS, 0x4e}, {SDL_SCANCODE_KP_EQUALS, 0x51}, {SDL_SCANCODE_KP_0, 0x52},
    {SDL_SCANCODE_KP_1, 0x53}, {SDL_SCANCODE_KP_2, 0x54}, {SDL_SCANCODE_KP_3, 0x55}, {SDL_SCANCODE_KP_4, 0x56},
    {SDL_SCANCODE_KP_5, 0x57}, {SDL_SCANCODE_KP_6, 0x58}, {SDL_SCANCODE_KP_7, 0x59}, {SDL_SCANCODE_KP_8, 0x5b},
    {SDL_SCANCODE_KP_9, 0x5c},
    {SDL_SCANCODE_F5, 0x60}, {SDL_SCANCODE_F6, 0x61}, {SDL_SCANCODE_F7, 0x62}, {SDL_SCANCODE_F3, 0x63},
    {SDL_SCANCODE_F8, 0x64}, {SDL_SCANCODE_F9, 0x65}, {SDL_SCANCODE_F11, 0x67}, {SDL_SCANCODE_F13, 0x69},
    {SDL_SCANCODE_F14, 0x6b}, {SDL_SCANCODE_F10, 0x6d}, {SDL_SCANCODE_F12, 0x6f}, {SDL_SCANCODE_F15, 0x71},
    {SDL_SCANCODE_INSERT, 0x72}, {SDL_SCANCODE_HELP, 0x72}, {SDL_SCANCODE_HOME, 0x73},
    {SDL_SCANCODE_PAGEUP, 0x74}, {SDL_SCANCODE_DELETE, 0x75}, {SDL_SCANCODE_F4, 0x76}, {SDL_SCANCODE_END, 0x77},
    {SDL_SCANCODE_F2, 0x78}, {SDL_SCANCODE_PAGEDOWN, 0x79}, {SDL_SCANCODE_F1, 0x7a}, {SDL_SCANCODE_LEFT, 0x7b},
    {SDL_SCANCODE_RIGHT, 0x7c}, {SDL_SCANCODE_DOWN, 0x7d}, {SDL_SCANCODE_UP, 0x7e},
};
}  // namespace

namespace tpw {

uint8_t MacKeyCodeFromScancode(int32_t sdlScancode) {
    for (const ScancodeKey& key : kScancodeKeys)
        if (key.scancode == sdlScancode) return key.macKey;
    return 0xff;
}

// The key a Windows virtual key stands for. Letters and digits are their own codes in
// Win32; the rest goes through macdoze's table to a Mac key and from there to a scancode.
int32_t ScancodeFromWindowsKey(int32_t virtualKey) {
    if (virtualKey >= 'A' && virtualKey <= 'Z') return SDL_SCANCODE_A + (virtualKey - 'A');
    if (virtualKey >= '1' && virtualKey <= '9') return SDL_SCANCODE_1 + (virtualKey - '1');
    if (virtualKey == '0') return SDL_SCANCODE_0;
    if (virtualKey < 0 || virtualKey > 0xff) return 0;
    const uint8_t macKey = kWindowsToMacKey[virtualKey];
    if (macKey == 0) return 0;   // Mac key 0 is A, which was handled above
    for (const ScancodeKey& key : kScancodeKeys)
        if (key.macKey == macKey) return key.scancode;
    return 0;
}

// Mac EventRecord modifier bits: command 0x100, shift 0x200, caps lock 0x400, option
// 0x800, control 0x1000. The host's Control key is reported as the Mac's control, and both
// the GUI (command) key and Control work for the game's shortcuts (TPMain.cpp folds 0x100
// and 0x1000 together).
uint16_t MacModifiersFromSDL(uint16_t sdlModifiers) {
    uint16_t modifiers = 0;
    if (sdlModifiers & SDL_KMOD_GUI) modifiers |= 0x100;
    if (sdlModifiers & SDL_KMOD_SHIFT) modifiers |= 0x200;
    if (sdlModifiers & SDL_KMOD_CAPS) modifiers |= 0x400;
    if (sdlModifiers & SDL_KMOD_ALT) modifiers |= 0x800;
    if (sdlModifiers & SDL_KMOD_CTRL) modifiers |= 0x1000;
    return modifiers;
}

}  // namespace tpw

namespace NS_MacDoze {

uint16_t MacToWindowsVirtualKey(uint8_t macKeyCode, uint16_t& flags) {
    const uint16_t entry = kMacToWindowsKey[macKeyCode];
    flags = entry & 0xff00;
    return entry & 0xff;
}

// Nonzero while the key is down. 1, 2 and 4 are the left, right and middle mouse buttons.
int16_t GetAsyncKeyState(int32_t virtualKey) {
    if (virtualKey == 1 || virtualKey == 2 || virtualKey == 4) {
        float scriptedX, scriptedY;
        bool scriptedDown;
        if (tpw::ScriptedPointer(scriptedX, scriptedY, scriptedDown))   // a scripted click holds the left or the right button
            return (virtualKey == 1 && scriptedDown) || (virtualKey == 2 && tpw::ScriptedRightButton()) ? static_cast<int16_t>(0x8000) : 0;
        const SDL_MouseButtonFlags buttons = SDL_GetMouseState(nullptr, nullptr);
        const SDL_MouseButtonFlags mask =
            virtualKey == 1 ? SDL_BUTTON_LMASK : virtualKey == 2 ? SDL_BUTTON_RMASK : SDL_BUTTON_MMASK;
        return (buttons & mask) != 0 ? static_cast<int16_t>(0x8000) : 0;
    }
    const int32_t scancode = tpw::ScancodeFromWindowsKey(virtualKey);
    if (scancode == 0) return 0;
    int count = 0;
    const bool* keys = SDL_GetKeyboardState(&count);
    if (keys == nullptr || scancode >= count) return 0;
    bool down = keys[scancode] || tpw::ScriptedKeyDown(scancode);
    // Either key of a pair counts for the modifiers.
    if (scancode == SDL_SCANCODE_LSHIFT) down = down || keys[SDL_SCANCODE_RSHIFT];
    if (scancode == SDL_SCANCODE_LCTRL) down = down || keys[SDL_SCANCODE_RCTRL];
    if (scancode == SDL_SCANCODE_LALT) down = down || keys[SDL_SCANCODE_RALT];
    return down ? static_cast<int16_t>(0x8000) : 0;
}

int16_t GetKeyState(int32_t virtualKey) { return GetAsyncKeyState(virtualKey); }

}  // namespace NS_MacDoze
