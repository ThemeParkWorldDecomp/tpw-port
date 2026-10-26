// gl_backend.cpp: the OpenGL backend of gfx.h: the context SDL makes on the game's window, the offscreen framebuffers of
// platform/offscreen.cpp when the window is fitted, and the swap. What platform/window.cpp did with OpenGL before the
// backends were split (tpw-port's Linux port): on a Mac and on Windows the calls below are the system's OpenGL; on
// Linux they are platform/gl_entry.cpp's, which hands them to the system's OpenGL opened through SDL.
#include <SDL3/SDL.h>

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <string>

#include "gfx.h"
#include "offscreen.h"
#include "platform.h"

// The OpenGL calls this file makes (the game's own are in platform/opengl.h).
extern "C" {
void glPixelStorei(unsigned int name, int value);
void glReadBuffer(unsigned int mode);
void glReadPixels(int x, int y, int width, int height, unsigned int format, unsigned int type, void* pixels);
const unsigned char* glGetString(unsigned int name);
}

namespace tpw::gfx::gl {

namespace {
constexpr unsigned int kPackAlignment = 0x0D05, kBack = 0x0405, kRgb = 0x1907, kUnsignedByte = 0x1401;
constexpr unsigned int kColorAttachment0 = 0x8CE0, kVendor = 0x1F00, kRenderer = 0x1F01, kVersion = 0x1F02;

SDL_Window* gWindow = nullptr;
SDL_GLContext gContext = nullptr;
bool gFit = false;
int gWide = 0, gHigh = 0;
std::string gDescription;
bool gProbed = false;
int gLargest = 0;

// A legacy (compatibility) context: the engine draws with OpenGL 1.1 calls.
void SetAttributes() {
    SDL_GL_SetAttribute(SDL_GL_RED_SIZE, 8);
    SDL_GL_SetAttribute(SDL_GL_GREEN_SIZE, 8);
    SDL_GL_SetAttribute(SDL_GL_BLUE_SIZE, 8);
    SDL_GL_SetAttribute(SDL_GL_ALPHA_SIZE, 8);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
}

std::string Describe() {
    auto text = [](unsigned int name) {
        const unsigned char* value = glGetString(name);
        return std::string(value != nullptr ? reinterpret_cast<const char*>(value) : "?");
    };
    return "OpenGL " + text(kVersion) + " on " + text(kRenderer) + " (" + text(kVendor) + ")";
}
}  // namespace

bool Probe(int& largest) {
    largest = gLargest;
    if (gProbed) return true;
    gProbed = true;
    SetAttributes();
    SDL_Window* probe = SDL_CreateWindow("", 1, 1, SDL_WINDOW_OPENGL | SDL_WINDOW_HIDDEN);
    if (probe == nullptr) {
        tpw::Log("opengl: no OpenGL window (%s)", SDL_GetError());
        return false;
    }
    bool ok = false;
    if (SDL_GLContext context = SDL_GL_CreateContext(probe)) {
        OnGLContextMade();
        gLargest = offscreen::LargestSize();
        tpw::Log("opengl: %s; frames up to %d pixels", Describe().c_str(), gLargest);
        SDL_GL_MakeCurrent(probe, nullptr);
        SDL_GL_DestroyContext(context);
        ok = true;
    } else {
        tpw::Log("opengl: no context (%s)", SDL_GetError());
    }
    SDL_DestroyWindow(probe);
    largest = gLargest;
    return ok;
}

uint32_t WindowFlags() {
    SetAttributes();
    return SDL_WINDOW_OPENGL;
}

bool Create(SDL_Window* window, bool fit, int wide, int high) {
    if (gContext != nullptr) Destroy();
    gWindow = window;
    gContext = SDL_GL_CreateContext(window);
    if (gContext == nullptr) {
        tpw::Log("SDL_GL_CreateContext failed: %s", SDL_GetError());
        return false;
    }
    SDL_GL_MakeCurrent(window, gContext);
    OnGLContextMade();
    SDL_GL_SetSwapInterval(1);
    gDescription = Describe();
    // The game's picture goes into a framebuffer of its own size from here on (see EndPresent); the fit is dropped by the
    // caller when the framebuffer cannot be made.
    Fit(fit, wide, high);
    return true;
}

void Destroy() {
    if (gContext == nullptr) return;
    offscreen::Destroy();
    SDL_GL_MakeCurrent(gWindow, nullptr);
    SDL_GL_DestroyContext(gContext);
    gContext = nullptr;
    gWindow = nullptr;
    gFit = false;
}

bool Created() { return gContext != nullptr; }

bool Fit(bool fit, int wide, int high) {
    if (gContext == nullptr) return false;
    gWide = wide;
    gHigh = high;
    if (!fit) {
        offscreen::Destroy();
        gFit = false;
        return true;
    }
    gFit = offscreen::Create(wide, high);
    return gFit;
}

bool FitReady() { return gContext != nullptr && gFit && offscreen::Ready(); }

bool SetSwapInterval(int interval) { return SDL_GL_SetSwapInterval(interval); }

void BeginPresent() {
    if (!FitReady()) return;
    offscreen::CopyGameToPresent();
    offscreen::BindPresent();
}

bool ReadPresent(uint8_t* rgb, int width, int height) {
    if (gContext == nullptr || width <= 0 || height <= 0) return false;
    glPixelStorei(kPackAlignment, 1);
    glReadBuffer(FitReady() ? kColorAttachment0 : kBack);   // (the present framebuffer is bound between Begin and End)
    glReadPixels(0, 0, width, height, kRgb, kUnsignedByte, rgb);
    return true;
}

void EndPresent(const fit::PixelBox& box, int windowWide, int windowHigh) {
    if (gContext == nullptr) return;
    if (FitReady()) {
        offscreen::ScaleToWindow(box, windowWide, windowHigh);
        // The swap presents nothing while a framebuffer object is bound (macOS, 2026-10-13); the game's is rebound after.
        offscreen::BindDefault();
        offscreen::DrainErrors("presenting a frame");
        SDL_GL_SwapWindow(gWindow);
        offscreen::BindGame();
        return;
    }
    SDL_GL_SwapWindow(gWindow);
}

const char* Description() { return gDescription.c_str(); }

}  // namespace tpw::gfx::gl

#ifndef __linux__
namespace tpw::gfx {
void OnGLContextMade() {}   // Linux: platform/gl_entry.cpp takes the system's OpenGL calls then
}  // namespace tpw::gfx
#endif
