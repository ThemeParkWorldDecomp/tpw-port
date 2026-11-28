#include "tpw_probe.h"
// gamework.cpp: UGamework, the application object of Sam's Utilities, and its run loop, on
// SDL events. ThemeParkWorld (TPMain.cpp) derives from it; the original's loop is
// UGamework::Run (sams utils 0x1000438c) with the dispatch in UGamework::HandleEvent
// (0x10004fa4): one pass handles the pending events, then calls Process.
#include <SDL3/SDL.h>

#include <algorithm>
#include <cmath>
#include <cctype>
#include <vector>

#include "fit.h"
#include "platform.h"
#include "platform/online_boss.h"
#ifdef __ANDROID__
#include "android/mobile_controls.h"
#endif

namespace SamsUtilities {

namespace {
#if defined(__APPLE__)
constexpr bool kApple = true;    // Cmd+Ctrl+F and the green button toggle full screen (Cmd+Ctrl+F the game's own with Spaces off); Alt+Enter is the game's
#else
constexpr bool kApple = false;   // Alt+Enter toggles full screen (platform/window.cpp ToggleFullScreen)
#endif

// What the original keeps in the object beyond the fields the game reads: the key that is
// down (UGamework +0x34 character, +0x35 key code, +0x38 modifiers), and the SDL key it came from.
struct KeyState {
    char character = 0;
    char keyCode = 0;
    uint32_t modifiers = 0;
    int32_t scancode = 0;
};
KeyState gKey;
int32_t gFrames = 0;
#ifdef TPW_PORT_FIX
// With the launcher's "Several keys at once" (tpw::gPortSeveralKeys): the keys that went down before the remembered one
// (gKey) and are still down, each with the modifiers it went down with.
std::vector<KeyState> gHeldKeys;

// TPW_PORT_FIX wasd-stuck UGamework::Run | Mac: 0x100054c4 tests the remembered key's key code, so a held A (key code 0) is not released before the next key goes down, and that key's key-up then leaves nothing remembered for A's own key-up to release: in the game A stayed down | Port: with the launcher's "W A S D move the camera" on (gPortReleaseKeyA, gPortStrayKeyUp), A is released before the next key as every other key is, and a key-up that releases no key of its own is passed to the game, which lets go of what that key still holds; off, the Mac's | Why: with A held, any other key going down (W, S, D, Escape) left the camera scrolling left for good after A was let go, even through a trip to the lobby (reported by the maintainer)
// TPW_PORT_FIX keya UGamework::Run | Mac: (the wasd-stuck note above) a held A is not released before the next key goes down | Port: the launcher sets tpw::gPortReleaseKeyA whether "W A S D move the camera" is on or off, so A is released first as every other key is | Why: a letter goes to an edit box at its key-up, and the A that was never released was never typed: a name typed quickly lost an "a" whenever the next key went down before A came up (the PC original typed it); contributed by samueleagle91 (tpw-port PR #132)
// Whether a key is remembered that a key going down releases first.
bool KeyStillDown() {
    return gKey.keyCode != 0 || (tpw::gPortReleaseKeyA && gKey.character != 0);
}
// Whether this key-up releases the remembered key as its own key.
bool OwnKeyUp(int32_t scancode) {
    return gKey.scancode == scancode && (gKey.character != 0 || gKey.keyCode != 0);
}
void StrayKeyUp(int32_t scancode) {
    if (tpw::gPortStrayKeyUp == nullptr) return;
    const uint8_t macKey = tpw::MacKeyCodeFromScancode(scancode);
    if (macKey != 0xff) tpw::gPortStrayKeyUp(macKey);
}

// TPW_PORT_FIX wasd-diagonal UGamework::Run | Mac: 0x100054c4 releases the key that is down before it presses the next one, and 0x100056a0 releases it at any key's key-up, so two camera keys were never down at once | Port: with the launcher's "W A S D move the camera" on (tpw::gPortKeyTogether names the four by their Mac key codes), one of the four going down while another of the four is down keeps it down, as "Several keys at once" keeps every key, and each of the four comes up at its own key-up; any other key going down releases the four first, the original's way, and with the setting off nothing changes | Why: W with A (any two of the four) scroll the park diagonally, and in first person walk and turn at once, as the maintainer asked
// Whether a key is one of the four that stay down together.
bool KeyTogether(int32_t scancode) {
    if (tpw::gPortKeyTogether == nullptr) return false;
    const uint8_t macKey = tpw::MacKeyCodeFromScancode(scancode);
    return macKey != 0xff && tpw::gPortKeyTogether(macKey);
}
// Whether this key going down keeps the remembered key down: both of the four, or no key remembered.
bool KeysTogether(int32_t scancode) {
    return KeyTogether(scancode) && (!KeyStillDown() || KeyTogether(gKey.scancode));
}
// TPW_PORT_FIX typing-order UGamework::Run | Mac: (one key at a time: no key is held with another) | Port: the key-up of a key among the held keys (gHeldKeys) releases that key, as a key that stays down together does, whatever KeyTogether says now | Why: whether keys stay down together is asked at the key-up, and an edit box can take the keyboard while two keys are held: the held key's key-up then took the one-key-at-a-time way, released the remembered key (the other one, still down) and left the held one down until the end of the pass
// Whether a key is among the held keys (gHeldKeys).
bool KeyHeld(int32_t scancode) {
    for (const KeyState& held : gHeldKeys) {
        if (held.scancode == scancode) return true;
    }
    return false;
}
#endif

// The autoKey events the Mac's Event Manager sent while the key that went down last stayed down: the first after the
// system's delay, then one at each interval (UGamework::Run takes an autoKey as a key-down, 0x100054c4). The port
// makes them itself, as the Event Manager did, and SDL's own repeats are not used. A scripted key's times are counted
// in passes at 30 a second with the fallback timing, so a scripted run repeats the same on every machine.
struct AutoKey {
    int32_t scancode = 0;   // the SDL key; 0 when none repeats
    bool scripted = false;  // a scripted key (--key, --hold)
    uint64_t due = 0;       // the time of the next autoKey, in milliseconds
    uint32_t intervalMs = 0;
};
AutoKey gAutoKey;

uint64_t AutoKeyClock(bool scripted) {
    return scripted ? static_cast<uint64_t>(gFrames + 1) * 1000u / 30u : SDL_GetTicks();
}

AutoKey StartAutoKey(SDL_Scancode scancode, bool scripted) {
    const tpw::KeyRepeatTiming timing = scripted ? tpw::KeyRepeatTiming() : tpw::SystemKeyRepeat();
    AutoKey autoKey;
    autoKey.scancode = static_cast<int32_t>(scancode);
    autoKey.scripted = scripted;
    autoKey.due = AutoKeyClock(scripted) + timing.delayMs;
    autoKey.intervalMs = timing.intervalMs;
    return autoKey;
}

// Shift, Control, Option (Alt), Command (the Windows key) and Caps Lock. The Mac's Event Manager sent no key-down or
// key-up for them, only the modifier bits of other events, so they never took or released the remembered key. The
// game still sees them: in each event's modifiers, and through GetAsyncKeyState, which reads SDL's keyboard state.
// From tpw-port PR #87 by samueleagle91.
bool IsModifierKey(SDL_Scancode scancode) {
    switch (scancode) {
    case SDL_SCANCODE_LSHIFT:
    case SDL_SCANCODE_RSHIFT:
    case SDL_SCANCODE_LCTRL:
    case SDL_SCANCODE_RCTRL:
    case SDL_SCANCODE_LALT:
    case SDL_SCANCODE_RALT:
    case SDL_SCANCODE_LGUI:
    case SDL_SCANCODE_RGUI:
    case SDL_SCANCODE_CAPSLOCK:
        return true;
    default:
        return false;
    }
}

// The scripted pointer (tpw::Config::clicks): where a scripted click put it and whether its button is still down.
struct ScriptedState {
    bool active = false;
    float x = 0;
    float y = 0;
    bool down = false;
    bool rightDown = false;
};
ScriptedState gScripted;

// The Mac character code of a key event: the layout's character with the modifiers
// applied, or the Mac's control character for a key without one.
char MacCharacter(const SDL_KeyboardEvent& key) {
    switch (key.scancode) {
    case SDL_SCANCODE_RETURN: return 0x0d;
    case SDL_SCANCODE_KP_ENTER: return 0x03;
    case SDL_SCANCODE_BACKSPACE: return 0x08;
    case SDL_SCANCODE_TAB: return 0x09;
    case SDL_SCANCODE_ESCAPE: return 0x1b;
    case SDL_SCANCODE_LEFT: return 0x1c;
    case SDL_SCANCODE_RIGHT: return 0x1d;
    case SDL_SCANCODE_UP: return 0x1e;
    case SDL_SCANCODE_DOWN: return 0x1f;
    case SDL_SCANCODE_DELETE: return 0x7f;
    case SDL_SCANCODE_HOME: return 0x01;
    case SDL_SCANCODE_END: return 0x04;
    case SDL_SCANCODE_PAGEUP: return 0x0b;
    case SDL_SCANCODE_PAGEDOWN: return 0x0c;
    case SDL_SCANCODE_INSERT: return 0x05;
    default: break;
    }
    const SDL_Keycode code = SDL_GetKeyFromScancode(key.scancode, key.mod, false);
    if (code >= 0x20 && code < 0x7f) return static_cast<char>(code);
#ifdef TPW_PORT_FIX
    // TPW_PORT_FIX typedletters MacCharacter | Mac: the Event Manager's character, in Mac Roman | Port: a key whose character
    // is a Latin-1 letter or sign (0xa0 to 0xff: an a, o or u with umlaut, a sharp s, an e with acute, an n with tilde) gives
    // that byte, its own UTF-16 code, as the PC's WM_CHAR gave the character of its code page 1252; with Option or Alt held
    // it stays none, as the Mac's Option characters are (Option-1 is a cheat key, not a character) | Why: the edit boxes keep
    // the character as UTF-16 (InterfaceEditBox::Edit, through TPMain.cpp's HandleTyping); before, such a key typed nothing
    if (code >= 0xa0 && code <= 0xff && (key.mod & SDL_KMOD_ALT) == 0) return static_cast<char>(code);
#endif
    if (key.scancode >= SDL_SCANCODE_F1 && key.scancode <= SDL_SCANCODE_F12) return 0x10;
    return 0;
}

// An event at a game pixel (the Mac's global coordinates: the window's origin plus the pixel).
tpw::Event MakeGameEvent(int32_t gameX, int32_t gameY) {
    tpw::Event event;
    int32_t originX, originY;
    tpw::WindowOrigin(originX, originY);
    event.globalX = originX + gameX;
    event.globalY = originY + gameY;
    event.modifiers = tpw::MacModifiersFromSDL(static_cast<uint16_t>(SDL_GetModState() | tpw::ScriptedSDLModifiers()));
    return event;
}

tpw::Event MakeEvent(float x, float y) {
    // With the fit the window is not the game's size: through the letterbox, into the game's pixels.
    double gameX = x, gameY = y;
    tpw::MouseToGame(x, y, gameX, gameY);
    return MakeGameEvent(static_cast<int32_t>(gameX), static_cast<int32_t>(gameY));
}

tpw::Event MakeEventAtMouse() {
    float x = 0, y = 0;
    SDL_GetMouseState(&x, &y);
    return MakeEvent(x, y);
}

#ifdef TPW_PORT_FIX
// pointerleave: SDL said the pointer left the window, and it has not moved over the window since. While it is set the
// game is told the pointer is out at each pass, not only when it left: the game keeps the pointer while a button is held
// (a drag), and a window that took the pointer for the press lets go of it only when the game handles the release, a
// pass later. On a Mac the leave can come during the press; told only then, the game kept the pointer at the window's
// edge after the release, and the camera scrolled toward that edge until the pointer came back.
bool gPointerOutside = false;

// pointerleave: whether a mouse event's point (window coordinates) is outside its window.
bool OutsideWindow(SDL_WindowID id, float x, float y) {
    SDL_Window* window = SDL_GetWindowFromID(id);
    int wide = 0, high = 0;
    if (window == nullptr || !SDL_GetWindowSize(window, &wide, &high)) return false;
    return x < 0.0f || y < 0.0f || x >= static_cast<float>(wide) || y >= static_cast<float>(high);
}

// focuspointer: the last pointer move the run loop had (window coordinates), also while the game was inactive.
bool gLastMoveKnown = false;
float gLastMoveX = 0.0f;
float gLastMoveY = 0.0f;

// TPW_PORT_FIX freecam UGamework::Run | Mac: (none: every button and wheel event went to the game) | Port: with the launcher's Free camera on (tpw::gPortDragStart and its hooks, set before the game starts), a button the camera takes as a drag holds the mouse in SDL's relative mode, its moves go to the camera and the game is told nothing; the drag ends, and the pointer is given back where it was, at the button's key-up, at the focus loss, or when the camera says it may not go on (a panel opened, first person); a drag whose quick click goes to the game sends that click at the key-up; the wheel's precise deltas go to the camera first (tpw::gPortWheel); off, none of it | Why: the Free camera's drags and trackpad, as SPEC asks
struct FreeDrag {
    bool active = false;        // the camera's drag, in relative mouse mode
    bool held = false;          // its button is still down (an ended drag's key-up is the camera's too)
    uint8_t sdlButton = 0;
    MouseButton button = kMouseButton0;
    SDL_Window* window = nullptr;
    float x = 0.0f, y = 0.0f;   // where it started, in window coordinates
    uint64_t startMs = 0;
    float moved = 0.0f;         // how far it moved, in pixels along both axes
    bool clickThrough = false;  // a quick click goes to the game
};
FreeDrag gFreeDrag;
constexpr uint64_t kFreeClickMs = 250;
constexpr float kFreeClickMoved = 6.0f;

// Ends the camera's drag: relative mode off and the pointer back where the drag started; the button stays held.
void EndFreeDrag() {
    if (!gFreeDrag.active) return;
    gFreeDrag.active = false;
    if (gFreeDrag.window != nullptr) {
        SDL_SetWindowRelativeMouseMode(gFreeDrag.window, false);
        SDL_WarpMouseInWindow(gFreeDrag.window, gFreeDrag.x, gFreeDrag.y);
    }
    if (tpw::gPortDragEnd != nullptr) tpw::gPortDragEnd();
}

// Whether a fine wheel delta is fractional (a trackpad's or a high-resolution wheel's).
bool Fractional(float value) { return value != static_cast<float>(static_cast<int32_t>(value)); }
#endif
#ifdef TPW_PORT_FIX
// TPW_PORT_FIX gamepad UGamework::Run | Mac: (none: the Mac version took no gamepad) | Port: with the launcher's Free camera and Gamepad on (tpw::gPortPadAxis and its hooks, set before the game starts) SDL's gamepad subsystem opens at the start, each gamepad is opened as it connects (one connected at the start too) and closed as it goes, and its axes and buttons, in SDL's standard layout, go to the free camera; an unplugged pad and the focus loss let go of what the pads hold; a scripted virtual gamepad (--pad) comes through the same events; off, no gamepad subsystem and no gamepad events | Why: the Free camera from a gamepad
struct Pads {
    bool system = false;                  // SDL's gamepad subsystem is open
    std::vector<SDL_Gamepad*> open;
    int32_t axisEvents = 0;               // what the pads sent, for the log when one goes
    int32_t buttonEvents = 0;
    SDL_JoystickID scriptedId = 0;        // the scripted virtual gamepad (--pad), and its joystick for setting it
    SDL_Joystick* scripted = nullptr;
};
Pads gPads;

// Opens SDL's gamepad subsystem, once. SDL then sends SDL_EVENT_GAMEPAD_ADDED for each pad already connected.
void OpenPadSystem() {
    if (gPads.system) return;
    // The scripted pad drives a window that is not in front (a test run never takes the focus): its events count.
    if (!tpw::config().pads.empty()) SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS, "1");
    if (!SDL_InitSubSystem(SDL_INIT_GAMEPAD)) {
        tpw::Log("gamepad: SDL's gamepad subsystem did not open (%s); no gamepad", SDL_GetError());
        return;
    }
    gPads.system = true;
    tpw::Log("gamepad: SDL's gamepad subsystem open");
}

void OpenPad(SDL_JoystickID id) {
    if (SDL_GetGamepadFromID(id) != nullptr) return;
    SDL_Gamepad* pad = SDL_OpenGamepad(id);
    if (pad == nullptr) {
        tpw::Log("gamepad: %d did not open (%s)", static_cast<int>(id), SDL_GetError());
        return;
    }
    gPads.open.push_back(pad);
    const char* name = SDL_GetGamepadName(pad);
    tpw::Log("gamepad: %s connected", name != nullptr ? name : "a gamepad");
}

void ClosePad(SDL_JoystickID id) {
    SDL_Gamepad* pad = SDL_GetGamepadFromID(id);
    if (pad == nullptr) return;
    tpw::Log("gamepad: disconnected (%d axis and %d button events)", gPads.axisEvents, gPads.buttonEvents);
    gPads.open.erase(std::remove(gPads.open.begin(), gPads.open.end(), pad), gPads.open.end());
    SDL_CloseGamepad(pad);
    if (tpw::gPortPadGone != nullptr) tpw::gPortPadGone();
}

// One step of the scripted gamepad (--pad), on its pass: a virtual pad of SDL's standard layout, set as a real one moves.
void ScriptedPadStep(const tpw::Config::ScriptedPad& step, int32_t pass) {
    if (step.kind == 0) {
        OpenPadSystem();
        if (!gPads.system || gPads.scripted != nullptr) return;
        SDL_VirtualJoystickDesc desc;
        SDL_INIT_INTERFACE(&desc);
        desc.type = SDL_JOYSTICK_TYPE_GAMEPAD;
        desc.naxes = SDL_GAMEPAD_AXIS_COUNT;
        desc.nbuttons = SDL_GAMEPAD_BUTTON_COUNT;
        desc.name = "Scripted gamepad";
        gPads.scriptedId = SDL_AttachVirtualJoystick(&desc);
        gPads.scripted = gPads.scriptedId != 0 ? SDL_OpenJoystick(gPads.scriptedId) : nullptr;
        tpw::Log("scripted gamepad: attached%s (pass %d)", gPads.scripted != nullptr ? "" : ", but it did not open", pass);
        return;
    }
    if (gPads.scripted == nullptr) {
        tpw::Log("scripted gamepad: none attached (pass %d)", pass);
        return;
    }
    if (step.kind == 1) {
        SDL_CloseJoystick(gPads.scripted);
        SDL_DetachVirtualJoystick(gPads.scriptedId);
        gPads.scripted = nullptr;
        gPads.scriptedId = 0;
        tpw::Log("scripted gamepad: detached (pass %d)", pass);
    } else if (step.kind == 2) {
        // A trigger's joystick axis runs the whole range for SDL's 0 to 32767.
        const bool trigger = step.index == SDL_GAMEPAD_AXIS_LEFT_TRIGGER || step.index == SDL_GAMEPAD_AXIS_RIGHT_TRIGGER;
        const int32_t raw = trigger ? std::clamp(step.value * 2 - 32768, -32768, 32767) : step.value;
        SDL_SetJoystickVirtualAxis(gPads.scripted, step.index, static_cast<int16_t>(raw));
        tpw::Log("scripted gamepad: axis %d to %d (pass %d)", step.index, step.value, pass);
    } else {
        SDL_SetJoystickVirtualButton(gPads.scripted, step.index, step.value != 0);
        tpw::Log("scripted gamepad: button %d %s (pass %d)", step.index, step.value != 0 ? "down" : "up", pass);
    }
}
#endif

bool MouseButtonFromSDL(uint8_t sdlButton, MouseButton& button) {
    switch (sdlButton) {
    case SDL_BUTTON_LEFT: button = kMouseButton0; return true;
    case SDL_BUTTON_RIGHT: button = kMouseButton1; return true;
    case SDL_BUTTON_MIDDLE: button = kMouseButton2; return true;
    default: return false;
    }
}
}  // namespace

UGamework::UGamework(int32_t) : mRunning(true), mValue70(0) {
    UMicroseconds rate = {0};
    mTimer.SetRate(rate, true);
}

UGamework::~UGamework() {}

bool UGamework::Initialise() { return true; }
void UGamework::Shutdown() {}
bool UGamework::Process() { return true; }
void UGamework::HandleMouseMoved(const UEvent&) {}
void UGamework::HandleMouseUp(const UEvent&, MouseButton) {}
void UGamework::HandleMouseDown(const UEvent&, MouseButton) {}
void UGamework::HandleMouseWheel(const UEvent&, int32_t) {}
void UGamework::HandleTyping(char, char, uint32_t, bool) {}
void UGamework::HandleUpdateWindow(GrafPort*) {}
void UGamework::HandleActivateWindow(GrafPort*, bool) {}

// 0x10005a64: command 3 is Quit.
void UGamework::HandleCommand(int32_t command, uint32_t) {
    if (command == 3) mRunning = false;
}

void UGamework::Run() {
    if (!Initialise()) return;
#ifdef __ANDROID__
    tpw::mobile::InstallControlEvents();
#endif
    // 0x100056a0, and the start of 0x100054c4: the remembered key comes up with the modifiers it went down with.
    auto releaseKey = [this]() {
        HandleTyping(gKey.character, gKey.keyCode, gKey.modifiers, false);
        gKey = KeyState();
    };
    // The rest of 0x100054c4: the key goes down with its event's modifiers and is remembered with them. False when the
    // game is given no key: Command-Q is the Quit command, and a key with no Mac key code is none.
    auto pressKey = [this](const SDL_KeyboardEvent& key) {
        const uint8_t macKey = tpw::MacKeyCodeFromScancode(key.scancode);
        if (macKey == 0xff) return false;
        const uint16_t modifiers = tpw::MacModifiersFromSDL(static_cast<uint16_t>(key.mod));
        if ((modifiers & 0x100) != 0 && key.scancode == SDL_SCANCODE_Q) {   // Command-Q
            HandleCommand(3, modifiers);
            return false;
        }
        gKey.character = MacCharacter(key);
        gKey.keyCode = static_cast<char>(macKey);
        gKey.modifiers = modifiers;
        gKey.scancode = static_cast<int32_t>(key.scancode);
        HandleTyping(gKey.character, gKey.keyCode, gKey.modifiers, true);
        return true;
    };
#ifdef TPW_PORT_FIX
    // TPW_PORT_FIX severalkeys UGamework::Run | Mac: 0x100054c4 releases the key that is down before it presses the next one, and 0x100056a0 releases it at any key's key-up, so the game saw one key at a time (Up held, then Left: first person turns and stops walking) | Port: with the launcher's "Several keys at once" on (gPortSeveralKeys, off by default) a key that goes down keeps the keys already down (gHeldKeys) and each comes up at its own key-up, as the PC's window messages did; off, the Mac's | Why: walking and turning at once in first person, as on the PC (reported by Addable13)
    // A held key's own key-up: released with the modifiers it went down with. False when this key is not held.
    auto releaseHeldKey = [this, &releaseKey](int32_t scancode) {
        if (gKey.scancode == scancode && (gKey.character != 0 || gKey.keyCode != 0)) {
            releaseKey();
            return true;
        }
        for (auto held = gHeldKeys.begin(); held != gHeldKeys.end(); ++held) {
            if (held->scancode != scancode) continue;
            HandleTyping(held->character, held->keyCode, held->modifiers, false);
            gHeldKeys.erase(held);
            return true;
        }
        return false;
    };
    // Before a key goes down: the remembered key stays down, among the held keys. The same key down again with no key-up
    // seen comes up first.
    auto keepKeys = [&releaseHeldKey](int32_t scancode) {
        releaseHeldKey(scancode);
        if (gKey.character != 0 || gKey.keyCode != 0) gHeldKeys.push_back(gKey);
        gKey = KeyState();
    };
    // wasd-diagonal: before any other key goes down, the four that are still down come up, each with the modifiers it
    // went down with (one key at a time: the list holds only them).
    auto releaseHeldKeys = [this]() {
        for (const KeyState& held : gHeldKeys) HandleTyping(held.character, held.keyCode, held.modifiers, false);
        gHeldKeys.clear();
    };
#endif
#ifdef TPW_PORT_FIX
    if (tpw::gPortPadAxis != nullptr) OpenPadSystem();   // gamepad: the launcher's Gamepad on
#endif
    while (mRunning) {
#ifdef TPW_PORT_FIX
        if (gPointerOutside && !gScripted.active && tpw::gPortPointerLeft != nullptr) tpw::gPortPointerLeft();   // pointerleave
        if (gFreeDrag.active && (tpw::gPortDragGoing == nullptr || !tpw::gPortDragGoing())) EndFreeDrag();   // freecam
#endif
        SDL_Event sdl;
        while (mRunning && SDL_PollEvent(&sdl)) {
#ifdef __ANDROID__
            if (tpw::mobile::HandleSliderDrag(sdl)) continue;
#endif
            switch (sdl.type) {
            case SDL_EVENT_QUIT:
            case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
                // The Quit menu command; the game asks for confirmation where it wants to.
                HandleCommand(3, tpw::MacModifiersFromSDL(static_cast<uint16_t>(SDL_GetModState())));
                break;
            case SDL_EVENT_MOUSE_MOTION: {
#ifdef TPW_PORT_FIX
                if (gFreeDrag.active) {   // freecam: the drag's moves are the camera's, and the game's pointer stays put
                    gFreeDrag.moved += std::fabs(sdl.motion.xrel) + std::fabs(sdl.motion.yrel);
                    if (tpw::gPortDragMove != nullptr) tpw::gPortDragMove(sdl.motion.xrel, sdl.motion.yrel);
                    break;
                }
#endif
                gScripted.active = false;   // the real mouse takes over from a scripted click
                const tpw::Event event = MakeEvent(sdl.motion.x, sdl.motion.y);
                HandleMouseMoved(event);
#ifdef TPW_PORT_FIX
                if (!OutsideWindow(sdl.motion.windowID, sdl.motion.x, sdl.motion.y)) gPointerOutside = false;   // pointerleave
                gLastMoveKnown = true;   // focuspointer
                gLastMoveX = sdl.motion.x;
                gLastMoveY = sdl.motion.y;
#endif
                break;
            }
            case SDL_EVENT_MOUSE_BUTTON_DOWN:
            case SDL_EVENT_MOUSE_BUTTON_UP: {
                MouseButton button;
                if (!MouseButtonFromSDL(sdl.button.button, button)) break;
#ifdef __ANDROID__
                if (button == kMouseButton0 && tpw::mobile::ShareSliderButton(sdl.button.down)) break;
#endif
                const tpw::Event event = MakeEvent(sdl.button.x, sdl.button.y);
#ifdef TPW_PORT_FIX
                // freecam: a button the camera takes, and its key-up
                if (sdl.type == SDL_EVENT_MOUSE_BUTTON_UP && gFreeDrag.held && sdl.button.button == gFreeDrag.sdlButton) {
                    const bool wasActive = gFreeDrag.active;
                    EndFreeDrag();
                    gFreeDrag.held = false;
                    if (wasActive && gFreeDrag.clickThrough && gFreeDrag.moved <= kFreeClickMoved &&
                        SDL_GetTicks() - gFreeDrag.startMs <= kFreeClickMs) {
                        const tpw::Event click = MakeEvent(gFreeDrag.x, gFreeDrag.y);
                        HandleMouseMoved(click);
                        HandleMouseDown(click, gFreeDrag.button);
                        HandleMouseUp(click, gFreeDrag.button);
                    }
                    break;
                }
                if (sdl.type == SDL_EVENT_MOUSE_BUTTON_DOWN && tpw::gPortDragStart != nullptr && !gFreeDrag.active) {
                    const int taken = tpw::gPortDragStart(static_cast<int>(button), event.modifiers);
                    if (taken != 0) {
                        gFreeDrag = FreeDrag();
                        gFreeDrag.active = gFreeDrag.held = true;
                        gFreeDrag.sdlButton = sdl.button.button;
                        gFreeDrag.button = button;
                        gFreeDrag.window = SDL_GetWindowFromID(sdl.button.windowID);
                        gFreeDrag.x = sdl.button.x;
                        gFreeDrag.y = sdl.button.y;
                        gFreeDrag.startMs = SDL_GetTicks();
                        gFreeDrag.clickThrough = taken == 2;
                        if (gFreeDrag.window != nullptr) SDL_SetWindowRelativeMouseMode(gFreeDrag.window, true);
                        break;
                    }
                }
#endif
                if (sdl.type == SDL_EVENT_MOUSE_BUTTON_DOWN) HandleMouseDown(event, button);
                else HandleMouseUp(event, button);
                break;
            }
            case SDL_EVENT_MOUSE_WHEEL: {
                // Whole steps only: SDL adds up a trackpad's or a fine wheel's fractions into integer_y. The float y of a
                // small movement truncated to 0, which the camera took as a step forward. From tpw-port PR #20 by
                // samueleagle91.
#ifdef TPW_PORT_FIX
                // freecam: the precise deltas go to the camera first, a trackpad's fractions among them
                if (tpw::gPortWheel != nullptr &&
                    tpw::gPortWheel(sdl.wheel.x, sdl.wheel.y, Fractional(sdl.wheel.x) || Fractional(sdl.wheel.y),
                                    tpw::MacModifiersFromSDL(static_cast<uint16_t>(SDL_GetModState() | tpw::ScriptedSDLModifiers())),
                                    SDL_GetTicks()))
                    break;
#endif
                if (sdl.wheel.integer_y == 0) break;
                const tpw::Event event = MakeEventAtMouse();
                HandleMouseWheel(event, static_cast<int32_t>(sdl.wheel.integer_y));
                break;
            }
#ifdef TPW_PORT_FIX
            // gamepad: a pad connects (or was connected at the start), goes, moves an axis, presses a button
            case SDL_EVENT_GAMEPAD_ADDED:
                if (gPads.system) OpenPad(sdl.gdevice.which);
                break;
            case SDL_EVENT_GAMEPAD_REMOVED:
                ClosePad(sdl.gdevice.which);
                break;
            case SDL_EVENT_GAMEPAD_AXIS_MOTION:
                ++gPads.axisEvents;
                if (tpw::gPortPadAxis != nullptr) tpw::gPortPadAxis(sdl.gaxis.axis, sdl.gaxis.value);
                break;
            case SDL_EVENT_GAMEPAD_BUTTON_DOWN:
            case SDL_EVENT_GAMEPAD_BUTTON_UP:
                ++gPads.buttonEvents;
                if (tpw::gPortPadButton != nullptr) tpw::gPortPadButton(sdl.gbutton.button, sdl.gbutton.down);
                break;
#endif
#if defined(TPW_PORT_FIX) && SDL_VERSION_ATLEAST(3, 4, 0)
            // TPW_PORT_FIX freecam pinch | Mac: (none) | Port: SDL 3.4's trackpad pinch goes to the Free camera's hook (zoom in orbit, a push in fly) | Why: SPEC's pinch; older SDLs have no pinch event, so the case is left out there
            case SDL_EVENT_PINCH_UPDATE:
                if (tpw::gPortPinch != nullptr) tpw::gPortPinch(sdl.pinch.scale);
                break;
#endif
            case SDL_EVENT_KEY_DOWN: {
#ifdef __ANDROID__
                if (tpw::mobile::IgnorePrintableTextKey(sdl.key)) break;
#endif
                // Alt+Enter on Windows and Linux toggles full screen (fit::FullScreenToggleKey; never on a Mac). It is
                // the host's key: the game is not told of the press, no key is remembered for it, and its release is
                // the ordinary one (whichever key came up releases the remembered key, below).
                if (tpw::fit::FullScreenToggleKey(
                        kApple, sdl.key.scancode == SDL_SCANCODE_RETURN || sdl.key.scancode == SDL_SCANCODE_KP_ENTER,
                        (sdl.key.mod & SDL_KMOD_ALT) != 0,
                        (sdl.key.mod & (SDL_KMOD_SHIFT | SDL_KMOD_CTRL | SDL_KMOD_GUI)) != 0, sdl.key.repeat)) {
                    tpw::ToggleFullScreen();
                    break;
                }
                // Cmd+Ctrl+F on a Mac whose full screen is not a Space (Fill Around Camera; fit::MacFullScreenKey): the same.
                if (tpw::fit::MacFullScreenKey(
                        kApple, !SDL_GetHintBoolean(SDL_HINT_VIDEO_MAC_FULLSCREEN_SPACES, true), sdl.key.scancode == SDL_SCANCODE_F,
                        (sdl.key.mod & SDL_KMOD_GUI) != 0 && (sdl.key.mod & SDL_KMOD_CTRL) != 0,
                        (sdl.key.mod & (SDL_KMOD_SHIFT | SDL_KMOD_ALT)) != 0, sdl.key.repeat)) {
                    tpw::ToggleFullScreen();
                    break;
                }
                // The host's repeats are not the Mac's (the autoKeys are made below), and a modifier is no key.
                if (sdl.key.repeat || IsModifierKey(sdl.key.scancode)) break;
                gAutoKey = AutoKey();
#ifdef TPW_PORT_FIX
                // severalkeys: the keys already down stay down; wasd-diagonal: one of the four keeps another of the four
                if (tpw::gPortSeveralKeys || KeysTogether(static_cast<int32_t>(sdl.key.scancode))) {
                    keepKeys(static_cast<int32_t>(sdl.key.scancode));
                    if (pressKey(sdl.key)) gAutoKey = StartAutoKey(sdl.key.scancode, false);
                    break;
                }
                releaseHeldKeys();   // wasd-diagonal
#endif
                // 0x100054c4: a key still down is released first (the Mac reported one key
                // at a time), with a Process pass in between.
#ifdef TPW_PORT_FIX
                if (KeyStillDown()) {   // wasd-stuck
#else
                if (gKey.keyCode != 0) {
#endif
                    releaseKey();
                    if (!Process()) {
                        mRunning = false;
                        break;
                    }
                }
                if (pressKey(sdl.key)) gAutoKey = StartAutoKey(sdl.key.scancode, false);
                break;
            }
            case SDL_EVENT_KEY_UP:
#ifdef __ANDROID__
                if (tpw::mobile::IgnorePrintableTextKey(sdl.key)) break;
#endif
                // 0x100056a0: releases the remembered key, whichever key came up. The Event Manager repeated the key
                // that went down last until that key came up.
                if (IsModifierKey(sdl.key.scancode)) break;
                if (sdl.key.scancode == gAutoKey.scancode) gAutoKey = AutoKey();
#ifdef TPW_PORT_FIX
                // severalkeys: only the key that came up; wasd-diagonal: one of the four, only itself; typing-order: a held key
                if (tpw::gPortSeveralKeys || KeyTogether(static_cast<int32_t>(sdl.key.scancode)) ||
                    KeyHeld(static_cast<int32_t>(sdl.key.scancode))) {
                    if (!releaseHeldKey(static_cast<int32_t>(sdl.key.scancode))) StrayKeyUp(sdl.key.scancode);   // wasd-stuck
                    break;
                }
                if (!OwnKeyUp(sdl.key.scancode)) StrayKeyUp(sdl.key.scancode);   // wasd-stuck
#endif
                if (gKey.character != 0 || gKey.keyCode != 0) releaseKey();
                break;
#ifdef __ANDROID__
            case SDL_EVENT_TEXT_INPUT:
                tpw::mobile::CommitText(sdl.text.text);
                break;
            case SDL_EVENT_SCREEN_KEYBOARD_HIDDEN:
                tpw::mobile::KeyboardHidden();
                break;
#endif
            case SDL_EVENT_WINDOW_FOCUS_GAINED:
                HandleActivateWindow(tpw::MainWindow(), true);
#ifdef TPW_PORT_FIX
                // TPW_PORT_FIX focuspointer UGamework::Run | Mac: (the game filled the screen, and while another program was in front the pointer was that program's) | Port: when the window gets the focus back, the game is told again of the pointer's last move, unless the pointer left the window since (gPointerOutside) or a scripted pointer has its place | Why: the game's UI drops the pointer's moves while the game is inactive (UI_DevicePointerMove), and SDL reports no move to where the pointer already is; moved away from a window edge while another window had the focus, the pointer stayed at that edge for the game after it came back, and once unpaused the camera scrolled toward that edge until the mouse moved (reported by slappyhead); contributed by samueleagle91 (tpw-port PR #133)
                if (gLastMoveKnown && !gPointerOutside && !gScripted.active) HandleMouseMoved(MakeEvent(gLastMoveX, gLastMoveY));
#endif
                break;
            case SDL_EVENT_WINDOW_FOCUS_LOST:
#ifdef __ANDROID__
                tpw::mobile::ParkFocusWillBeLost();
#endif
#ifdef TPW_PORT_FIX
                EndFreeDrag();   // freecam
                if (tpw::gPortPadGone != nullptr) tpw::gPortPadGone();   // gamepad
#endif
                HandleActivateWindow(tpw::MainWindow(), false);
                break;
#ifdef TPW_PORT_FIX
            case SDL_EVENT_WINDOW_MOUSE_LEAVE:
                // The pointer left the window (the game in a window, or full screen with another screen beside it): the
                // game takes it out of all of its windows until it moves over the game again (tpw::gPortPointerLeft,
                // pointerleave). On the Mac the game filled the screen, and the pointer never left it. A scripted click
                // keeps its place. The game is told again at each pass until the pointer is back (gPointerOutside).
                gPointerOutside = true;
                if (!gScripted.active && tpw::gPortPointerLeft != nullptr) tpw::gPortPointerLeft();
                break;
#endif
#ifdef TPW_PORT_FIX
            // TPW_PORT_FIX fullscreengrab UGamework::Run | Mac: the game took the whole screen, and the pointer never left it | Port: while the game's window is a full-screen window SDL keeps the pointer inside it (SDL_SetWindowMouseGrab, fit::ConfinePointer; SDL lets go while the game is not the active application), on every system, and in a window the pointer moves freely; set at SDL's full-screen events, whichever switch made them (the window made full screen, the Esc menu's button, Alt+Enter, Cmd+Ctrl+F or the green button) | Why: on a computer with several screens the pointer went off the full-screen game onto the screen beside it, where a click took the game out of the front (reported by BMCTV)
            case SDL_EVENT_WINDOW_ENTER_FULLSCREEN:
            case SDL_EVENT_WINDOW_LEAVE_FULLSCREEN:
                if (SDL_Window* window = SDL_GetWindowFromID(sdl.window.windowID)) {
                    const bool confine = tpw::fit::ConfinePointer(sdl.type == SDL_EVENT_WINDOW_ENTER_FULLSCREEN, tpw::config().background);
                    if (SDL_SetWindowMouseGrab(window, confine)) tpw::Log("fullscreen: the pointer is %s", confine ? "kept in the full-screen window" : "free to leave the window");
                    else tpw::Log("fullscreen: the pointer could not be %s (%s)", confine ? "kept in the window" : "let go", SDL_GetError());
                }
                break;
#endif
            default:
                break;
            }
        }
        if (!mRunning) break;
        // The scripted clicks (--click): pass N moves the pointer and presses, pass N + 1 releases.
        for (const tpw::Config::ScriptedClick& click : tpw::config().clicks) {
            const int32_t pass = gFrames + 1;
            if (click.pass != pass && click.pass + 1 != pass) continue;
            const tpw::Event event = MakeGameEvent(click.x, click.y);
            if (click.pass == pass) {
                gScripted.active = true;
                gScripted.x = static_cast<float>(click.x);
                gScripted.y = static_cast<float>(click.y);
                gScripted.down = true;
                tpw::Log("scripted click: pointer to (%d, %d), button down (pass %d)", click.x, click.y, pass);
                HandleMouseMoved(event);
                HandleMouseDown(event, kMouseButton0);
            } else {
                gScripted.down = false;
                tpw::Log("scripted click: button up (pass %d)", pass);
                HandleMouseUp(event, kMouseButton0);
            }
        }
        // The scripted moves (--move): the pointer only.
        for (const tpw::Config::ScriptedMove& move : tpw::config().moves) {
            if (move.pass != gFrames + 1) continue;
            gScripted.active = true;
            gScripted.x = static_cast<float>(move.x);
            gScripted.y = static_cast<float>(move.y);
            tpw::Log("scripted move: pointer to (%d, %d) (pass %d)", move.x, move.y, gFrames + 1);
            HandleMouseMoved(MakeGameEvent(move.x, move.y));
        }
        // The scripted right clicks (--rclick): as the clicks, with the right button.
        for (const tpw::Config::ScriptedClick& click : tpw::config().rightClicks) {
            const int32_t pass = gFrames + 1;
            if (click.pass != pass && click.pass + 1 != pass) continue;
            const tpw::Event event = MakeGameEvent(click.x, click.y);
            if (click.pass == pass) {
                gScripted.active = true;
                gScripted.x = static_cast<float>(click.x);
                gScripted.y = static_cast<float>(click.y);
                gScripted.rightDown = true;
                tpw::Log("scripted right click: pointer to (%d, %d), button down (pass %d)", click.x, click.y, pass);
                HandleMouseMoved(event);
                HandleMouseDown(event, kMouseButton1);
            } else {
                gScripted.rightDown = false;
                tpw::Log("scripted right click: button up (pass %d)", pass);
                HandleMouseUp(event, kMouseButton1);
            }
        }
        // The scripted keys (--key, --hold): down and up as the SDL key events above send them.
        for (const tpw::Config::ScriptedKey& key : tpw::config().keys) {
            const int32_t pass = gFrames + 1;
            if (key.down != pass && key.up != pass) continue;
            SDL_KeyboardEvent sdlKey{};
            sdlKey.scancode = static_cast<SDL_Scancode>(key.scancode);
            sdlKey.mod = static_cast<SDL_Keymod>(tpw::ScriptedSDLModifiers());
            tpw::Log("scripted key: %s %s (pass %d)", SDL_GetScancodeName(sdlKey.scancode),
                     key.down == pass ? "down" : "up", pass);
            if (IsModifierKey(sdlKey.scancode)) continue;   // only in the modifiers (ScriptedSDLModifiers)
            if (key.down == pass) {
                gAutoKey = AutoKey();
#ifdef TPW_PORT_FIX
                if (tpw::gPortSeveralKeys || KeysTogether(key.scancode)) {   // severalkeys, wasd-diagonal
                    keepKeys(key.scancode);
                } else {
                    releaseHeldKeys();   // wasd-diagonal
                }
                if (KeyStillDown()) releaseKey();   // wasd-stuck
#else
                if (gKey.keyCode != 0) releaseKey();
#endif
                if (pressKey(sdlKey)) gAutoKey = StartAutoKey(sdlKey.scancode, true);
            } else {
                if (sdlKey.scancode == gAutoKey.scancode) gAutoKey = AutoKey();
#ifdef TPW_PORT_FIX
                // severalkeys, wasd-diagonal: only the key that came up; typing-order: a held key
                if (tpw::gPortSeveralKeys || KeyTogether(key.scancode) || KeyHeld(key.scancode)) {
                    if (!releaseHeldKey(key.scancode)) StrayKeyUp(key.scancode);   // wasd-stuck
                    continue;
                }
                if (!OwnKeyUp(key.scancode)) StrayKeyUp(key.scancode);   // wasd-stuck
#endif
                if (gKey.character != 0 || gKey.keyCode != 0) releaseKey();
            }
        }
#ifdef TPW_PORT_FIX
        // severalkeys: a held key the keyboard no longer holds, with no key-up seen, is released, as the autoKey's is below.
        for (size_t index = 0; index < gHeldKeys.size();) {
            const KeyState held = gHeldKeys[index];
            if (SDL_GetKeyboardState(nullptr)[held.scancode] || tpw::ScriptedKeyDown(held.scancode)) {
                ++index;
                continue;
            }
            gHeldKeys.erase(gHeldKeys.begin() + static_cast<std::ptrdiff_t>(index));
            HandleTyping(held.character, held.keyCode, held.modifiers, false);
        }
#endif
        if (!mRunning) break;
        // The autoKey of the key that went down last (AutoKey, above), at most one a pass. It is 0x100054c4 for that
        // key: the remembered key is released with the modifiers it went down with, then pressed and remembered with
        // the modifiers held now, so a modifier pressed or let go while the key is held reaches the game at the next
        // autoKey, as on the Mac. The release is skipped only when the key remembered has no key code and the same
        // modifiers ('A' is key code 0, which 0x100054c4 takes for no key: it is pressed again unreleased).
        if (gAutoKey.scancode != 0) {
            const SDL_Scancode scancode = static_cast<SDL_Scancode>(gAutoKey.scancode);
            const uint64_t now = AutoKeyClock(gAutoKey.scripted);
            if (!SDL_GetKeyboardState(nullptr)[scancode] && !tpw::ScriptedKeyDown(gAutoKey.scancode)) {
                // Up with no key-up seen (the system kept it): released as a key-up would.
                gAutoKey = AutoKey();
                if (gKey.character != 0 || gKey.keyCode != 0) releaseKey();
            } else if (now >= gAutoKey.due) {
                gAutoKey.due = now + gAutoKey.intervalMs;
                SDL_KeyboardEvent sdlKey{};
                sdlKey.scancode = scancode;
                sdlKey.mod = static_cast<SDL_Keymod>(SDL_GetModState() | tpw::ScriptedSDLModifiers());
                const uint16_t modifiers = tpw::MacModifiersFromSDL(static_cast<uint16_t>(sdlKey.mod));
                if (gKey.keyCode != 0 || (gKey.character != 0 && gKey.modifiers != modifiers)) {
                    releaseKey();
#ifdef TPW_PORT_FIX
                    // TPW_PORT_FIX autokey UGamework::Run | Mac: 0x100054c4 runs a Process pass between the release and the press of an autoKey | Port: the release and the press are in the same pass, one after the other in the game's message queue | Why: in that pass every held key read as up, so walking in first person and moving the camera with a held arrow key lost a frame at each repeat
#else
                    if (!Process()) mRunning = false;
#endif
                }
                if (mRunning && !pressKey(sdlKey)) gAutoKey = AutoKey();
            }
        }
        if (!mRunning) break;
        // The scripted drags (--drag): hover, press, move in steps, release.
        for (const tpw::Config::ScriptedDrag& drag : tpw::config().drags) {
            const int32_t step = gFrames + 1 - drag.pass;
            if (step < 0 || step > drag.steps + 4 || step == 1) continue;
            int32_t x = drag.x0, y = drag.y0;
            if (step > 2) {
                const int32_t moved = std::min(step - 2, drag.steps);
                x = drag.x0 + (drag.x1 - drag.x0) * moved / drag.steps;
                y = drag.y0 + (drag.y1 - drag.y0) * moved / drag.steps;
            }
            const tpw::Event event = MakeGameEvent(x, y);
            gScripted.active = true;
            gScripted.x = static_cast<float>(x);
            gScripted.y = static_cast<float>(y);
            HandleMouseMoved(event);
            if (step == 2) {
                gScripted.down = true;
                HandleMouseDown(event, kMouseButton0);
            } else if (step == drag.steps + 4) {
                gScripted.down = false;
                HandleMouseUp(event, kMouseButton0);
            }
            tpw::Log("scripted drag: pointer at (%d, %d)%s (pass %d)", x, y,
                     step == 2 ? ", button down" : step == drag.steps + 4 ? ", button up" : "", gFrames + 1);
        }
        // The scripted wheel events (--wheel): as SDL_EVENT_MOUSE_WHEEL above, at the pointer.
        for (const tpw::Config::ScriptedWheel& wheel : tpw::config().wheels) {
            if (wheel.pass != gFrames + 1) continue;
            tpw::Log("scripted wheel: %.3f, %.3f (pass %d)", wheel.x, wheel.y, gFrames + 1);
            const tpw::Event event = gScripted.active ? MakeGameEvent(static_cast<int32_t>(gScripted.x), static_cast<int32_t>(gScripted.y))
                                                      : MakeEventAtMouse();
#ifdef TPW_PORT_FIX
            if (tpw::gPortWheel != nullptr &&
                tpw::gPortWheel(wheel.x, wheel.y, Fractional(wheel.x) || Fractional(wheel.y), event.modifiers,
                                AutoKeyClock(true)))
                continue;
#endif
            const int32_t steps = static_cast<int32_t>(wheel.y);
            if (steps != 0) HandleMouseWheel(event, steps);
        }
#ifdef TPW_PORT_FIX
        // The scripted gamepad (--pad, gamepad): a virtual pad, whose events come through the queue above next pass.
        for (const tpw::Config::ScriptedPad& pad : tpw::config().pads) {
            if (pad.pass == gFrames + 1) ScriptedPadStep(pad, gFrames + 1);
        }
#endif
        // The scripted camera drags (--cam-drag): the button, the moves and the release as the real ones above go.
        for (const tpw::Config::ScriptedCamDrag& drag : tpw::config().camDrags) {
            const int32_t step = gFrames + 1 - drag.pass;
            if (step < 0 || step > drag.steps + 1) continue;
            const MouseButton button = drag.button == 0 ? kMouseButton0 : drag.button == 1 ? kMouseButton1 : kMouseButton2;
            const tpw::Event event = gScripted.active ? MakeGameEvent(static_cast<int32_t>(gScripted.x), static_cast<int32_t>(gScripted.y))
                                                      : MakeEventAtMouse();
#ifdef TPW_PORT_FIX
            static bool taken = false;
            if (step == 0) {
                taken = tpw::gPortDragStart != nullptr && tpw::gPortDragStart(drag.button, event.modifiers) != 0;
                tpw::Log("scripted camera drag: button %d down, %s (pass %d)", drag.button, taken ? "the camera's" : "the game's",
                         gFrames + 1);
                if (!taken) HandleMouseDown(event, button);
            } else if (step <= drag.steps) {
                if (taken && tpw::gPortDragGoing != nullptr && !tpw::gPortDragGoing()) {
                    tpw::Log("scripted camera drag: stopped by the camera (pass %d)", gFrames + 1);
                    if (tpw::gPortDragEnd != nullptr) tpw::gPortDragEnd();
                    taken = false;
                } else if (taken && tpw::gPortDragMove != nullptr) {
                    tpw::gPortDragMove(drag.dx / static_cast<float>(drag.steps), drag.dy / static_cast<float>(drag.steps));
                }
            } else {
                tpw::Log("scripted camera drag: button %d up (pass %d)", drag.button, gFrames + 1);
                if (taken) {
                    if (tpw::gPortDragEnd != nullptr) tpw::gPortDragEnd();
                    taken = false;
                } else {
                    HandleMouseUp(event, button);
                }
            }
#else
            if (step == 0) HandleMouseDown(event, button);
            else if (step == drag.steps + 1) HandleMouseUp(event, button);
#endif
        }
        // The scripted typing (--type): one key at a time, down on one pass and up on the next.
        for (const tpw::Config::ScriptedTyping& typing : tpw::config().typing) {
            const int32_t pass = gFrames + 1;
            const int32_t index = (pass - typing.pass) / 2;
            if (pass < typing.pass || index >= static_cast<int32_t>(typing.text.size())) continue;
            const char character = typing.text[static_cast<size_t>(index)];
            const bool down = (pass - typing.pass) % 2 == 0;
            // Letters, digits and space are their own virtual keys; other punctuation is no key, only the character (as '@' always
            // was): its code is a navigation key's ('.' is 0x2e, VK_DELETE, which the edit boxes took as forward delete).
            const unsigned char code = static_cast<unsigned char>(character);
            const int32_t windowsKey = character == '\r' ? 0x0d : std::isalnum(code) || character == ' ' ? std::toupper(code) : 0;
            const char macKey = static_cast<char>(tpw::MacKeyCodeFromScancode(
                static_cast<SDL_Scancode>(tpw::ScancodeFromWindowsKey(windowsKey))));
            const uint32_t modifiers = std::isupper(static_cast<unsigned char>(character)) ? 0x200u : 0u;
            tpw::Log("scripted typing: '%c' %s (pass %d)", typing.hidden ? '*' : character, down ? "down" : "up", pass);
            HandleTyping(character, macKey, modifiers, down);
        }
#ifdef TPW_PORT_FIX
        // TPW_PORT_FIX online-server UGamework::Run | Mac: the online library's comms thread called the game's online callbacks itself, at any moment | Port: the online server client's answers are given here, on the game's thread, once a pass before Process (none unless the launcher names a server) | Why: a callback that ran on another thread raced the game's online objects, which the callbacks change and may delete
        tpw::online::boss::Pump();
#endif
#ifdef __ANDROID__
        tpw::mobile::UpdateControls();
#endif
        if (!Process()) mRunning = false;
        if (tpw::config().probe) {
            const uint32_t* c = tpw_probe::gCounters;
            tpw::Log("probe pass %d: hfield %u skipped %u pages %u cells %u noprim %u | queued %u texexec %u pack %u upload %u | unrendered %u listed %u extern %u gldraw %u verts %u",
                     gFrames + 1, c[tpw_probe::kHFieldExecute], c[tpw_probe::kHFieldExecuteSkipped],
                     c[tpw_probe::kHFieldTextures], c[tpw_probe::kHFieldCells], c[tpw_probe::kHFieldPrimitiveNull],
                     c[tpw_probe::kTexQueued], c[tpw_probe::kTexExecute], c[tpw_probe::kPackTextures],
                     c[tpw_probe::kUploadToVRAM], c[tpw_probe::kExecuteUnrendered], c[tpw_probe::kRecordsListed],
                     c[tpw_probe::kExternExecute], c[tpw_probe::kGLDrawIndexed], c[tpw_probe::kGLVertices]);
            tpw::Log("probe pass %d: prepare cells %u hidden %u notexture %u | dac earlyout %u box x %d..%d z %d..%d map %dx%d rows %d..%d",
                     gFrames + 1, c[tpw_probe::kPrepareCells], c[tpw_probe::kPrepareHidden],
                     c[tpw_probe::kPrepareNoTexture], c[tpw_probe::kDacEarlyOut],
                     static_cast<int32_t>(c[tpw_probe::kDacX0]), static_cast<int32_t>(c[tpw_probe::kDacX1]),
                     static_cast<int32_t>(c[tpw_probe::kDacZ0]), static_cast<int32_t>(c[tpw_probe::kDacZ1]),
                     static_cast<int32_t>(c[tpw_probe::kDacWidth]), static_cast<int32_t>(c[tpw_probe::kDacDepth]),
                     static_cast<int32_t>(c[tpw_probe::kRowsFirst]), static_cast<int32_t>(c[tpw_probe::kRowsEnd]));
            tpw::Log("probe pass %d: mesh prepare %u calcentry %u | loadridetype %u instanceride %u failed %u | linkadd %u nonode %u dup %u listlinks %u list6 %u | texprepare %u null %u ignored %u held %u meshtexnull %u",
                     gFrames + 1, c[tpw_probe::kMeshPrepare], c[tpw_probe::kCalcRenderEntry],
                     c[tpw_probe::kLoadRideType], c[tpw_probe::kInstanceRide], c[tpw_probe::kInstanceRideFailed],
                     c[tpw_probe::kMapLinkAdd], c[tpw_probe::kMapLinkNoNode], c[tpw_probe::kMapLinkDuplicate],
                     c[tpw_probe::kPrepareListLinks], c[tpw_probe::kGlobalList6],
                     c[tpw_probe::kTexPrepare], c[tpw_probe::kTexPrepareNull], c[tpw_probe::kTexPrepareIgnored],
                     c[tpw_probe::kTexPrepareHeld], c[tpw_probe::kMeshTexNull]);
            for (uint32_t& value : tpw_probe::gCounters) value = 0;
        }
        if (tpw::config().maxFrames != 0 && ++gFrames >= tpw::config().maxFrames) {
            tpw::Log("stopping after %d passes (the launcher's limit)", gFrames);
            mRunning = false;
        }
    }
    Shutdown();
}

}  // namespace SamsUtilities

namespace tpw {
int32_t CurrentPass() { return SamsUtilities::gFrames + 1; }

bool ScriptedPointer(float& gameX, float& gameY, bool& buttonDown) {
    const SamsUtilities::ScriptedState& s = SamsUtilities::gScripted;
    if (!s.active) return false;
    gameX = s.x;
    gameY = s.y;
    buttonDown = s.down;
    return true;
}

bool ScriptedRightButton() { return SamsUtilities::gScripted.active && SamsUtilities::gScripted.rightDown; }

bool ScriptedKeyDown(int32_t sdlScancode) {
    const int32_t pass = CurrentPass();
    for (const Config::ScriptedKey& key : config().keys)
        if (key.scancode == sdlScancode && pass >= key.down && pass < key.up) return true;
    return false;
}

uint16_t ScriptedSDLModifiers() {
    uint16_t modifiers = 0;
    if (ScriptedKeyDown(SDL_SCANCODE_LGUI)) modifiers |= SDL_KMOD_LGUI;
    if (ScriptedKeyDown(SDL_SCANCODE_LCTRL)) modifiers |= SDL_KMOD_LCTRL;
    if (ScriptedKeyDown(SDL_SCANCODE_LSHIFT)) modifiers |= SDL_KMOD_LSHIFT;
    if (ScriptedKeyDown(SDL_SCANCODE_LALT)) modifiers |= SDL_KMOD_LALT;
    return modifiers;
}

int32_t ScancodeFromKeyName(const char* name) {
    const std::string n = name;
    if (n == "esc" || n == "escape") return SDL_SCANCODE_ESCAPE;
    if (n == "return") return SDL_SCANCODE_RETURN;
    if (n == "space") return SDL_SCANCODE_SPACE;
    if (n == "tab") return SDL_SCANCODE_TAB;
    if (n == "cmd" || n == "command") return SDL_SCANCODE_LGUI;
    if (n == "ctrl" || n == "control") return SDL_SCANCODE_LCTRL;
    if (n == "shift") return SDL_SCANCODE_LSHIFT;
    if (n == "option" || n == "alt") return SDL_SCANCODE_LALT;
    const SDL_Scancode code = SDL_GetScancodeFromName(name);
    return code == SDL_SCANCODE_UNKNOWN ? 0 : static_cast<int32_t>(code);
}
}  // namespace tpw

namespace SamsUtilities {

// ---- UDialog ---------------------------------------------------------------------------
// The game's only UDialog is the "cd missing" alert, which is never shown here: the game
// folder counts as the CD (files.cpp). Show logs and returns, as if dismissed.
UDialog::UDialog(const UResourceId&, bool) : mDefaultItem(1) {}
UDialog::~UDialog() {}
bool UDialog::Filter(const UEvent&, int16_t&) { return false; }
void UDialog::Show() { tpw::Log("UDialog::Show: a Mac dialog was requested and skipped"); }
// The dialog's text is the game's missing-CD message, set at every start before the CD check; the
// check passes on the game folder, so the text is not logged, only (once a run) what is true.
void UDialog::SetParamText(const UString&, const UString&, const UString&, const UString&) {
    static bool logged = false;
    if (logged) return;
    logged = true;
    tpw::Log("cd check: the game folder counts as the CD; the game's CD dialog is not shown");
}
void UDialog::SetItemTitle(int32_t, const UString&) {}

}  // namespace SamsUtilities
