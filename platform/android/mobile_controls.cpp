#include "mobile_controls.h"

#include <SDL3/SDL.h>
#include <jni.h>

#include <atomic>
#include <cstdint>

#include "Game.h"
#include "InterfaceScrollBar.h"
#include "InterfaceWindow.h"
#include "UI_Base.h"
#include "keytable_actions.h"
#include "port_wasd.h"
#include "ui_controls.h"
#include "ui_main.h"

namespace {
// Written by Android's UI thread and read by the SDL game thread.
std::atomic<uint32_t> gStick{0};
std::atomic<bool> gKeyboardRequested{false};
std::atomic<bool> gControlDialogOpen{false};
std::atomic<bool> gResumeParkRequested{false};
std::atomic<Uint32> gSliderDragEvent{0};
std::atomic<int> gTurns{0};
float gCameraX = 0, gCameraY = 0;
bool gParkPausedByFocus = false;
InterfaceWindow* gTouchSliderThumb = nullptr;
bool gSliderMouseHeld = false;

bool Editing() {
    return gUIKeyboardWindow != nullptr && gUIKeyboardWindow->WhatAmI() == kUIKindEditBox;
}

bool NotifyActivity(const char* method, bool value) {
    auto* env = static_cast<JNIEnv*>(SDL_GetAndroidJNIEnv());
    if (env == nullptr) return false;
    auto activity = static_cast<jobject>(SDL_GetAndroidActivity());
    if (activity == nullptr) return false;
    jclass type = env->GetObjectClass(activity);
    jmethodID notify = env->GetMethodID(type, method, "(Z)V");
    if (notify != nullptr) env->CallVoidMethod(activity, notify, static_cast<jboolean>(value));
    if (env->ExceptionCheck()) env->ExceptionClear();
    env->DeleteLocalRef(type);
    env->DeleteLocalRef(activity);
    return true;
}
}

extern "C" JNIEXPORT void JNICALL Java_org_tpwport_android_ThemeParkActivity_nativeCamera(JNIEnv*, jclass, jfloat x, jfloat y) {
    const auto pack = [](float axis) { return static_cast<uint16_t>(static_cast<int16_t>(axis * 4096)); };
    gStick.store(static_cast<uint32_t>(pack(x)) | (static_cast<uint32_t>(pack(y)) << 16), std::memory_order_relaxed);
}

extern "C" JNIEXPORT void JNICALL Java_org_tpwport_android_ThemeParkActivity_nativeKeyboard(JNIEnv*, jclass) {
    gKeyboardRequested.store(true, std::memory_order_relaxed);
}

extern "C" JNIEXPORT void JNICALL Java_org_tpwport_android_ThemeParkActivity_nativeRotate(JNIEnv*, jclass, jint direction) {
    if (direction == -1 || direction == 1) gTurns.fetch_add(direction, std::memory_order_relaxed);
}

extern "C" JNIEXPORT void JNICALL Java_org_tpwport_android_ThemeParkActivity_nativeControlDialog(JNIEnv*, jclass, jboolean opening) {
    gControlDialogOpen.store(opening, std::memory_order_relaxed);
}

extern "C" JNIEXPORT void JNICALL Java_org_tpwport_android_ThemeParkActivity_nativeResumePark(JNIEnv*, jclass) {
    gResumeParkRequested.store(true, std::memory_order_relaxed);
}

extern "C" JNIEXPORT void JNICALL Java_org_tpwport_android_ThemeParkActivity_nativeSliderDrag(JNIEnv*, jclass, jboolean start) {
    SDL_Event event{};
    event.type = gSliderDragEvent.load(std::memory_order_relaxed);
    if (event.type == 0) return;
    event.user.code = start;
    SDL_PushEvent(&event);
}

namespace tpw::mobile {

void InstallControlEvents() {
    gSliderDragEvent.store(SDL_RegisterEvents(1), std::memory_order_relaxed);
    SDL_SetEventFilter([](void*, SDL_Event* event) {
        // SDL suspends during the dialog. Skip the game's extra pause on losing focus to its own controls.
        return event->type != SDL_EVENT_WINDOW_FOCUS_LOST || !gControlDialogOpen.load(std::memory_order_relaxed);
    }, nullptr);
}

bool HandleSliderDrag(const SDL_Event& event) {
    if (event.type != gSliderDragEvent.load(std::memory_order_relaxed)) return false;
    if (event.user.code) {
        // The move preceding this event hit-tested the point where the finger first touched.
        if (gUIActive && gUICaptureDevice == nullptr && (gUIButtonFlags & 1) == 0 &&
            gUIPointerIn != nullptr && gUIPointerIn->mCallback == UI_DefaultThumbtrackCallback) {
            gTouchSliderThumb = gUIPointerIn;
            UI_DeviceButtonDown(0, gUIPointerX, gUIPointerY);
        }
    } else {
        if (!gSliderMouseHeld && gTouchSliderThumb != nullptr && gUIButtonWindow[0] == gTouchSliderThumb) {
            UI_DeviceButtonUp(0, gUIPointerX, gUIPointerY);
        }
        gTouchSliderThumb = nullptr;
        gSliderMouseHeld = false;
    }
    return true;
}

bool ShareSliderButton(bool down) {
    if (gTouchSliderThumb == nullptr || gUIButtonWindow[0] != gTouchSliderThumb) return false;
    gSliderMouseHeld = down;
    return true;
}

void ParkFocusWillBeLost() {
    // Called before the game pauses on focus loss. A park already paused keeps its own state.
    if (gGameWindow != nullptr && gGame.mF3C == 1 && gGame.mF1C == 0) gParkPausedByFocus = true;
}

void CameraAxes(float& x, float& y) {
    x = gCameraX;
    y = gCameraY;
}

void UpdateControls() {
    SDL_Window* window = SDL_GetKeyboardFocus();
    const bool park = window != nullptr && gGameWindow != nullptr && gUIKeyboardWindow == gGameWindow;
    static bool cameraShown = false;
    const bool havePark = gGameWindow != nullptr;
    if (havePark != cameraShown && NotifyActivity("cameraAvailable", havePark)) cameraShown = havePark;

    if (!havePark || gGame.mF1C == 0 || gGame.mF3C != 1) gParkPausedByFocus = false;
    const bool resumeRequested = gResumeParkRequested.exchange(false, std::memory_order_relaxed);
    if (resumeRequested && window != nullptr && gParkPausedByFocus) {
        gGame.Unpause();
        gParkPausedByFocus = false;
    }
    static bool resumeShown = false;
    const bool canResume = window != nullptr && gParkPausedByFocus;
    if (canResume != resumeShown && NotifyActivity("parkResumeAvailable", canResume)) resumeShown = canResume;

    const uint32_t encoded = park ? gStick.load(std::memory_order_relaxed) : 0;
    const float x = static_cast<int16_t>(encoded) / 4096.0f;
    const float y = static_cast<int16_t>(encoded >> 16) / 4096.0f;
    const bool walking = (gFirstPersonFlags & kFirstPersonAny) != 0;
    gCameraX = walking ? 0 : x;
    gCameraY = walking ? 0 : y;
    const bool directions[] = {walking && y < -0.18f, walking && x < -0.18f, walking && y > 0.18f, walking && x > 0.18f};
    static int16_t held[4] = {};
    for (unsigned i = 0; i < 4; ++i) {
        const int16_t key = directions[i] ? PortMovementCameraKey(i) : 0;
        if (key == held[i]) continue;
        if (held[i] != 0) gKeyTableCamera->KeyUp(held[i], 0);
        if (key != 0) gKeyTableCamera->KeyDown(key, 0);
        held[i] = key;
    }

    const int turns = gTurns.exchange(0, std::memory_order_relaxed) % 4;
    if (park && turns != 0 && gKeyTableGame->mEnabled != 0) {
        const auto rotate = turns > 0 ? turn_object_clockwise : turn_object_counterclockwise;
        for (int i = 0; i < (turns > 0 ? turns : -turns); ++i) rotate();
    }

    static const InterfaceWindow* previousEditor = nullptr;
    const InterfaceWindow* editor = window != nullptr && Editing() ? gUIKeyboardWindow : nullptr;
    const bool requested = gKeyboardRequested.exchange(false, std::memory_order_relaxed);
    if (editor != nullptr && (editor != previousEditor || requested) && !SDL_TextInputActive(window)) {
        const SDL_PropertiesID properties = SDL_CreateProperties();
        SDL_SetBooleanProperty(properties, SDL_PROP_TEXTINPUT_AUTOCORRECT_BOOLEAN, false);
        SDL_StartTextInputWithProperties(window, properties);
        SDL_DestroyProperties(properties);
    } else if (editor == nullptr && window != nullptr && SDL_TextInputActive(window)) {
        SDL_StopTextInput(window);
    }
    previousEditor = editor;
}

bool GeneratedTextKey(const SDL_KeyboardEvent& key) {
    // SDL's Android IME sends both commit text and global synthetic key events for ASCII.
    return key.which == 0 && key.key >= 0x20 && key.key < 0x7f && Editing();
}

void CommitText(const char* text) {
    while (Editing() && text != nullptr && *text != '\0') {
        const Uint32 character = SDL_StepUTF8(&text, nullptr);
        if (character >= 0x20 && character != 0x7f && character < 0x10000 && character != 0xfffd) {
            UI_KeyChar(static_cast<int16_t>(character), 0x1000000);
        }
    }
}

void KeyboardHidden() {
    if (SDL_Window* window = SDL_GetKeyboardFocus(); window != nullptr && SDL_TextInputActive(window)) {
        SDL_StopTextInput(window);
    }
}

}
