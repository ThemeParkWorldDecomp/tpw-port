#include "tpw_probe.h"
// gamework.cpp: UGamework, the application object of Sam's Utilities, and its run loop, on
// SDL events. ThemeParkWorld (TPMain.cpp) derives from it; the original's loop is
// UGamework::Run (sams utils 0x1000438c) with the dispatch in UGamework::HandleEvent
// (0x10004fa4): one pass handles the pending events, then calls Process.
#include <SDL3/SDL.h>

#include <algorithm>
#include <cctype>

#include "platform.h"

namespace SamsUtilities {

namespace {
// What the original keeps in the object beyond the fields the game reads: the key that is
// down (UGamework +0x34 character, +0x35 key code, +0x38 modifiers).
struct KeyState {
    char character = 0;
    char keyCode = 0;
    uint32_t modifiers = 0;
};
KeyState gKey;
int32_t gFrames = 0;

// The scripted pointer (tpw::Config::clicks): where a scripted click put it and whether its button is still down.
struct ScriptedState {
    bool active = false;
    float x = 0;
    float y = 0;
    bool down = false;
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
    event.modifiers = tpw::MacModifiersFromSDL(static_cast<uint16_t>(SDL_GetModState()));
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
    while (mRunning) {
        SDL_Event sdl;
        while (mRunning && SDL_PollEvent(&sdl)) {
            switch (sdl.type) {
            case SDL_EVENT_QUIT:
            case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
                // The Quit menu command; the game asks for confirmation where it wants to.
                HandleCommand(3, tpw::MacModifiersFromSDL(static_cast<uint16_t>(SDL_GetModState())));
                break;
            case SDL_EVENT_MOUSE_MOTION: {
                gScripted.active = false;   // the real mouse takes over from a scripted click
                const tpw::Event event = MakeEvent(sdl.motion.x, sdl.motion.y);
                HandleMouseMoved(event);
                break;
            }
            case SDL_EVENT_MOUSE_BUTTON_DOWN:
            case SDL_EVENT_MOUSE_BUTTON_UP: {
                MouseButton button;
                if (!MouseButtonFromSDL(sdl.button.button, button)) break;
                const tpw::Event event = MakeEvent(sdl.button.x, sdl.button.y);
                if (sdl.type == SDL_EVENT_MOUSE_BUTTON_DOWN) HandleMouseDown(event, button);
                else HandleMouseUp(event, button);
                break;
            }
            case SDL_EVENT_MOUSE_WHEEL: {
                // Whole steps only: SDL adds up a trackpad's or a fine wheel's fractions into integer_y. The float y of a
                // small movement truncated to 0, which the camera took as a step forward.
                if (sdl.wheel.integer_y == 0) break;
                const tpw::Event event = MakeEventAtMouse();
                HandleMouseWheel(event, static_cast<int32_t>(sdl.wheel.integer_y));
                break;
            }
            case SDL_EVENT_KEY_DOWN: {
                // 0x100054c4: a key still down is released first (the Mac reported one key
                // at a time), with a Process pass in between.
                if (!sdl.key.repeat && gKey.keyCode != 0) {
                    HandleTyping(gKey.character, gKey.keyCode, gKey.modifiers, false);
                    gKey = KeyState();
                    if (!Process()) {
                        mRunning = false;
                        break;
                    }
                }
                const uint8_t macKey = tpw::MacKeyCodeFromScancode(sdl.key.scancode);
                if (macKey == 0xff) break;
                const uint16_t modifiers = tpw::MacModifiersFromSDL(static_cast<uint16_t>(sdl.key.mod));
                if ((modifiers & 0x100) != 0 && sdl.key.scancode == SDL_SCANCODE_Q) {   // Command-Q
                    HandleCommand(3, modifiers);
                    break;
                }
                gKey.character = MacCharacter(sdl.key);
                gKey.keyCode = static_cast<char>(macKey);
                gKey.modifiers = modifiers;
                HandleTyping(gKey.character, gKey.keyCode, gKey.modifiers, true);
                break;
            }
            case SDL_EVENT_KEY_UP:
                // 0x100056a0: releases the remembered key, whichever key came up.
                if (gKey.character != 0 || gKey.keyCode != 0) {
                    HandleTyping(gKey.character, gKey.keyCode, gKey.modifiers, false);
                    gKey = KeyState();
                }
                break;
            case SDL_EVENT_WINDOW_FOCUS_GAINED:
                HandleActivateWindow(tpw::MainWindow(), true);
                break;
            case SDL_EVENT_WINDOW_FOCUS_LOST:
                HandleActivateWindow(tpw::MainWindow(), false);
                break;
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
            tpw::Log("scripted typing: '%c' %s (pass %d)", character, down ? "down" : "up", pass);
            HandleTyping(character, macKey, modifiers, down);
        }
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
}  // namespace tpw

namespace SamsUtilities {

// ---- UDialog ---------------------------------------------------------------------------
// The game's only UDialog is the "cd missing" alert, which is never shown here: the game
// folder counts as the CD (files.cpp). Show logs and returns, as if dismissed.
UDialog::UDialog(const UResourceId&, bool) : mDefaultItem(1) {}
UDialog::~UDialog() {}
bool UDialog::Filter(const UEvent&, int16_t&) { return false; }
void UDialog::Show() { tpw::Log("UDialog::Show: a Mac dialog was requested and skipped"); }
void UDialog::SetParamText(const UString& p0, const UString&, const UString&, const UString&) {
    tpw::Log("UDialog text: %s", p0.mText);
}
void UDialog::SetItemTitle(int32_t, const UString&) {}

}  // namespace SamsUtilities
